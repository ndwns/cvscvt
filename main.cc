#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>

#include "blob.h"
#include "date.h"
#include "heap.h"
#include "indent.h"
#include "lexer.h"
#include "piecetable.h"
#include "set.h"
#include "strutil.h"
#include "types.h"
#include "uptr.h"
#include "vector.h"

#define ATTIC "Attic"
#define CLEAR "\r\x1B[K"

#ifndef DEBUG_SPLIT
#	define DEBUG_SPLIT 0
#endif

using std::cerr;
using std::cout;
using std::endl;

enum OutputFormat
{
	OUT_GIT,
	OUT_SVN
};

static OutputFormat output_format = OUT_GIT;

namespace Sym
{
	static Symbol Exp;
	static Symbol access;
	static Symbol author;
	static Symbol branch;
	static Symbol branches;
	static Symbol comment;
	static Symbol date;
	static Symbol dead;
	static Symbol desc;
	static Symbol expand;
	static Symbol head;
	static Symbol locks;
	static Symbol log;
	static Symbol next;
	static Symbol state;
	static Symbol strict;
	static Symbol symbols;
	static Symbol text;
}

#undef major
#undef minor

struct RevNum
{
	RevNum(RevNum const* const pre, u4 const major, u4 const minor) :
		pre(pre),
		major(major),
		minor(minor)
	{}

	bool trunk() const { return !pre; }

	u4 hash() const { return ((pre ? pre->hash() * 31 : 0) + major * 31) + minor; }

	static RevNum const* parse(Symbol);

	RevNum const* const pre;
	u4            const major;
	u4            const minor;
};

static bool operator ==(RevNum const& a, RevNum const& b)
{
	return a.pre == b.pre && a.major == b.major && a.minor == b.minor;
}

static bool operator <(RevNum const& a, RevNum const& b)
{
	return a.pre == b.pre && (a.major < b.major || (a.major == b.major && a.minor < b.minor));
}

static Set<RevNum*> revnums;

RevNum const* RevNum::parse(Symbol const s)
{
	u1 const*       i   = s->data;
	u1 const* const end = i + s->size;

	RevNum const* rev = 0;
	for (;;) {
		if (i == end) goto invalid;

		u4 major;
		u4 minor;
		if ('1' <= *i && *i <= '9') {
			major = *i++ - '0';
			for (;;) {
				if (i == end) {
					minor = major;
					major = 0;
					goto done;
				}
				if ('0' <= *i && *i <= '9') {
					major = major * 10 + (*i++ - '0');
				} else {
					break;
				}
			}
		} else if (*i++ == '0') {
			major = 0;
			if (i == end) goto invalid;
		} else {
			goto invalid;
		}

		if (*i++ != '.') goto invalid;
		if (i == end)    goto invalid;

		if ('1' <= *i && *i <= '9') {
			minor = *i++ - '0';
			for (;;) {
				if (i != end && '0' <= *i && *i <= '9') {
					minor = minor * 10 + (*i++ - '0');
				} else {
					break;
				}
			}
		} else if (*i++ == '0') {
			minor = 0;
			if ('0' <= *i && *i <= '9') goto invalid;
		} else {
			goto invalid;
		}

done:
		rev = revnums.insert(new RevNum(rev, major, minor));
		if (i == end) return rev;

		if (*i++ != '.') goto invalid;
	}

invalid:
	throw std::runtime_error("invalid revision number");
}

static std::ostream& operator <<(std::ostream& o, RevNum const& r)
{
	if (r.pre) {
		o << *r.pre << '.';
	}
	return o << r.major << '.' << r.minor;
}

struct Directory
{
	Directory() :
		name(0),
		parent(0),
		n_entries(0)
	{}

	Directory(char const* const name, Directory* const parent) :
		name(strdup(name)),
		parent(parent),
		n_entries(0)
	{}

	char*      name;
	Directory* parent;
	size_t     n_entries;
};

static std::ostream& operator <<(std::ostream& o, Directory const& d)
{
	if (d.parent) {
		o << *d.parent;
	}
	return d.name ? o << d.name << '/' : o;
}

struct FileRev;

struct File
{
	File(char const* const name, Directory* const dir, bool const executable) :
		name(strdup(name)),
		dir(dir),
		executable(executable),
		head()
	{}

	u4 hash() const { return (uintptr_t)this >> 4; }

	char*      const name;
	Directory* const dir;
	bool       const executable;
	FileRev*         head;
};

static std::ostream& operator <<(std::ostream& o, File const& f)
{
	if (f.dir) {
		o << *f.dir;
	}
	return o << f.name;
}

static bool operator ==(File const& a, File const& b)
{
	return &a == &b;
}

enum State
{
	STATE_DEAD,
	STATE_EXP
};

#if DEBUG_SPLIT
static std::ostream& operator <<(std::ostream& o, State const s)
{
	char const* text = "<invalid>";
	switch (s) {
		case STATE_DEAD: text = "dead"; break;
		case STATE_EXP:  text = "Exp";  break;
	}
	return o << text;
}
#endif

struct Changeset;

struct FileRev
{
	FileRev(File const* file, RevNum const* const rev) :
		file(file),
		rev(rev),
		date(),
		author(),
		state(),
		log(),
		text(),
		pred(),
		next(),
		changeset(),
		mark()
	{}

	u4 hash() const { return rev->hash(); }

	File   const* file;
	RevNum const* rev;
	Date          date;
	Symbol        author;
	State         state;
	Symbol        log;
	Symbol        text;
	FileRev*      pred;
	FileRev*      next; // The next file revision on the same branch
	Changeset*    changeset;
	u4            mark;
	PieceTable    content;
};

static inline bool operator ==(FileRev const& a, FileRev const& b)
{
	return a.file == b.file && a.rev == b.rev;
}

struct Changeset
{
	Changeset(Symbol const log, Symbol const author) :
		log(log),
		author(author),
		oldest(9999, 12, 31, 23, 59, 59),
#if DEBUG_SPLIT
		newest(0, 1, 1, 0, 0, 0),
#endif
		filerevs(),
		n_succ()
	{}

	u4 hash() const { return log->hash() ^ author->hash(); }

	void add(FileRev* const f)
	{
		Date const& d = f->date;
		if (d < oldest)
			oldest = d;
#if DEBUG_SPLIT
		if (newest < d)
			newest = d;
#endif
		filerevs.push_back(f);
		f->changeset = this;
	}

	Symbol           log;
	Symbol           author;
	Date             oldest;
#if DEBUG_SPLIT
	Date             newest;
#endif
	Vector<FileRev*> filerevs;
	size_t           n_succ;
};

static inline bool operator ==(Changeset const& a, Changeset const& b)
{
	return a.log == b.log && a.author == b.author;
}

static char const*     trunk_name = 0;
static bool            verbose    = false;
static Set<Changeset*> changesets;
static size_t          file_revs;
static size_t          on_trunk;
static size_t          n_files;
static bool            in_attic;

static std::ostream& print_read_status()
{
	return cerr << CLEAR << n_files << " files, " << file_revs << " file revisions, " << on_trunk << " on trunk, " << changesets.size() << " changesets";
}

static void accept_newphrase(Lexer& l, Symbol const stop_at = 0)
{
	while (Symbol const sym = l.accept(T_ID)) {
		if (sym == stop_at) break;
		cerr << CLEAR "warning: ignoring newphrase '" << *sym << "'\n";
		while (l.accept(T_ID) || l.accept(T_NUM) || l.accept(T_STRING) || l.accept(T_COLON)) {}
		l.expect(T_SEMICOLON);
	}
}

static void read_file(FILE* const f, File* const file)
{
	Lexer l(f);

	Set<FileRev*> revs;

	/*
	 * rcstext   ::=  admin {delta}* desc {deltatext}*
	 *
	 * admin     ::=  head       {num};
	 *                { branch   {num}; }
	 *                access     {id}*;
	 *                symbols    {sym : num}*;
	 *                locks      {id : num}*;  {strict  ;}
	 *                { comment  {string}; }
	 *                { expand   {string}; }
	 *                { newphrase }*
	 *
	 * delta     ::=  num
	 *                date       num;
	 *                author     id;
	 *                state      {id};
	 *                branches   {num}*;
	 *                next       {num};
	 *                { newphrase }*
	 *
	 * desc      ::=  desc       string
	 *
	 * deltatext ::=  num
	 *                log        string
	 *                { newphrase }*
	 *                text       string
	 */

	l.expect(Sym::head);
	Symbol const shead = l.expect(T_NUM);
	l.expect(T_SEMICOLON);

	RevNum const* const head = RevNum::parse(shead);
	file->head = revs.insert(new FileRev(file, head));

	if (l.accept(Sym::branch)) {
		l.expect(T_NUM);
		l.expect(T_SEMICOLON);
	}

	l.expect(Sym::access);
	while (l.accept(T_ID)) {}
	l.expect(T_SEMICOLON);

	l.expect(Sym::symbols);
	while (l.accept(T_ID)) {
		l.expect(T_COLON);
		l.expect(T_NUM);
	}
	l.expect(T_SEMICOLON);

	l.expect(Sym::locks);
	while (l.accept(T_ID)) {
		l.expect(T_COLON);
		l.expect(T_NUM);
	}
	l.expect(T_SEMICOLON);

	if (l.accept(Sym::strict))
		l.expect(T_SEMICOLON);

	if (l.accept(Sym::comment)) {
		l.accept(T_STRING);
		l.expect(T_SEMICOLON);
	}

	if (l.accept(Sym::expand)) {
		l.accept(T_STRING);
		l.expect(T_SEMICOLON);
	}

	accept_newphrase(l);

	while (Symbol const srev = l.accept(T_NUM)) {
		l.expect(Sym::date);
		Symbol const sdate = l.expect(T_NUM);
		Date   const date(Date::parse(sdate));
		l.expect(T_SEMICOLON);

		l.expect(Sym::author);
		Symbol const sauthor = l.expect(T_ID);
		l.expect(T_SEMICOLON);

		l.expect(Sym::state);
		Symbol const sstate = l.accept(T_ID);
		l.expect(T_SEMICOLON);

		l.expect(Sym::branches);
		while (l.accept(T_NUM)) {}
		l.expect(T_SEMICOLON);

		l.expect(Sym::next);
		Symbol const snext = l.accept(T_NUM);
		l.expect(T_SEMICOLON);

		if (++file_revs % 100 == 0 && !verbose) {
			print_read_status() << ' ' << *file;
		}

		RevNum const* const rev = RevNum::parse(srev);
		if (rev->trunk()) {
			++on_trunk;
			FileRev* const filerev = revs.insert(new FileRev(file, rev));
			if (snext) {
				RevNum  const* const pred = RevNum::parse(snext);
				FileRev*       const prev = revs.insert(new FileRev(file, pred));

				if (prev->next) {
					cerr << CLEAR "warning: both " << *prev->next->rev << " and " << *rev << " of " << *file << " have " << *pred << " as predecessor\n";
				}

				filerev->pred = prev;
				prev->next    = filerev;
			}
			filerev->date   = date;
			filerev->author = sauthor;
			if (sstate == Sym::dead) {
				filerev->state = STATE_DEAD;
			} else {
				if (sstate != Sym::Exp) {
					cerr << CLEAR "warning: " << *file << ' ' << *rev << " has unknown state '" << *sstate << "'; treating as 'Exp'\n";
				}
				filerev->state = STATE_EXP;
			}
		}
	}

	if (!file->head->author) {
		cerr << CLEAR "error: head of " << *file << " does not exist\n";
	} else if (in_attic && file->head->state != STATE_DEAD) {
		cerr << CLEAR "warning: " << *file << " is in " ATTIC ", but head is not dead; treating as dead\n";
		file->head->state = STATE_DEAD;
	} else if (!in_attic && file->head->state == STATE_DEAD) {
		cerr << CLEAR "warning: " << *file << " is not in " ATTIC ", but head is dead\n";
	}

	accept_newphrase(l, Sym::desc);
	l.expect(T_STRING);

	while (Symbol const srev = l.accept(T_NUM)) {
		l.expect(Sym::log);
		Symbol const slog = l.expect(T_STRING);

		accept_newphrase(l, Sym::text);
		Symbol const stext = l.expect(T_STRING);

		RevNum const* const rev = RevNum::parse(srev);
		if (rev->trunk()) {
			FileRev* const filerev = revs.insert(new FileRev(file, rev));
			filerev->log  = slog;
			filerev->text = stext;

			Changeset* const changeset = changesets.insert(new Changeset(slog, filerev->author));
			changeset->add(filerev);
		}
	}

	l.expect(T_EOF);
}

#ifdef __APPLE__
typedef FTSENT const**       FTSENT_cmp;
#else
typedef FTSENT const* const* FTSENT_cmp;
#endif

static int compar(FTSENT_cmp const a, FTSENT_cmp const b)
{
	return
		streq((*a)->fts_name, ATTIC) ? -1 :
		streq((*b)->fts_name, ATTIC) ?  1 :
		strcmp((*a)->fts_name, (*b)->fts_name);
}

static bool older_changeset(Changeset const* const a, Changeset const* const b)
{
	return a->oldest < b->oldest;
}

static bool older_filerev(FileRev const* const a, FileRev const* const b)
{
	if (a->file == b->file) {
		return *a->rev < *b->rev;
	}
	return a->date < b->date;
}

static bool between(char const min, char const c, char const max)
{
	return min <= c && c <= max;
}

static char const* check_trunk_name(char const* const name)
{
	switch (*name) {
		case '\0': throw std::runtime_error("trunk name must not be empty");
		case '/':  throw std::runtime_error("trunk name must not start with a slash ('/')");
		case '-':  throw std::runtime_error("trunk name must not start with a minus ('-')");
		default:   break;
	}

	for (char const* i = name; *i != '\0'; ++i) {
		if (*i == '/') {
			switch (i[1]) {
				case '\0': throw std::runtime_error("trunk name must not end with a slash ('/')");
				case '/':  throw std::runtime_error("trunk name must not contain consecutive slashes ('//')");
			}
		} else if (!between('A', *i, 'Z') && !between('a', *i, 'z') && !between('0', *i, '9') && *i != '_' && *i != '+' && *i != '-' && *i != '.') {
			throw std::runtime_error("trunk name may only contain letters, digits, underscore, plus, minus and period");
		}
	}

	return name;
}

static bool is_executable(FILE* const f)
{
	struct stat stat_buf;
	if (fstat(fileno(f), &stat_buf) != 0) {
		std::runtime_error("fstat failed");
	}
	return stat_buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH);
}

static size_t log2(size_t const v)
{
	return
		v <         10 ? 1 :
		v <        100 ? 2 :
		v <       1000 ? 3 :
		v <      10000 ? 4 :
		v <     100000 ? 5 :
		v <    1000000 ? 6 :
		v <   10000000 ? 7 :
		v <  100000000 ? 8 :
		v < 1000000000 ? 9 :
		10;
}

static void add_dir_entry(Directory* const d)
{
	if (d && d->n_entries++ == 0) {
		add_dir_entry(d->parent);
		cout << "Node-path: " << trunk_name << '/' << *d << "\nNode-kind: dir\nNode-action: add\n\n";
	}
}

static void del_dir_entry(Directory* const d)
{
	if (d && --d->n_entries == 0) {
		cout << "Node-path: " << trunk_name << '/' << *d << "\nNode-kind: dir\nNode-action: delete\n\n";
		del_dir_entry(d->parent);
	}
}

static bool is_cont_byte(u1 const c)
{
	return 0x80 <= c && c < 0xC0;
}

static Blob* convert_log(Blob const& src)
{
	BlobBuilder     b;
	u1 const*       i      = src.begin();
	u1 const* const end    = src.end();
	u1 const*       lstart = i;
	u1 const*       lend   = i;
	bool            empty  = false;
	for (;;) {
		if (i == end) goto end;
		switch (*i++) {
			case '\t':
			case ' ':
				break;

			case '\r':
				if (i != end && *i == '\n') ++i;
				/* FALLTHROUGH */
			case '\n':
				// TODO support other encodings besides utf-8/l1 hybrid
end:
				if (lstart == lend) {
					empty = true;
				} else {
					if (empty) {
						empty = false;
						if (!b.empty()) b.add_byte('\n');
					}
					for (u1 const* k = lstart; k != lend;) {
						u1 const c = *k;
						if (c < 0x80) {
							b.add_byte(c);
							k += 1;
						} else if (c < 0xC2) {
							goto convert_byte;
						} else if (c < 0xE0) {
							if (lend - k < 2)        goto convert_byte;
							if (!is_cont_byte(k[1])) goto convert_byte;
							b.add_byte(c);
							b.add_byte(k[1]);
							k += 2;
						} else if (c < 0xF0) {
							if (lend - k < 3)        goto convert_byte;
							if (!is_cont_byte(k[1])) goto convert_byte;
							if (!is_cont_byte(k[2])) goto convert_byte;
							b.add_byte(c);
							b.add_byte(k[1]);
							b.add_byte(k[2]);
							k += 3;
						} else if (c < 0xF1) {
							if (lend - k < 4)        goto convert_byte;
							if (!is_cont_byte(k[1])) goto convert_byte;
							if (!is_cont_byte(k[2])) goto convert_byte;
							if (!is_cont_byte(k[3])) goto convert_byte;
							b.add_byte(c);
							b.add_byte(k[1]);
							b.add_byte(k[2]);
							b.add_byte(k[3]);
							k += 4;
						} else {
convert_byte:
							b.add_byte(0xC0 | c >> 6);
							b.add_byte(0x80 | (c & 0x3F));
							k += 1;
						}
					}
					b.add_byte('\n');
				}
				if (i == end) return b.get();
				lstart = lend = i;
				break;

			default:
				lend = i;
				break;
		}
	}
}

int main(int argc, char** argv)
try
{
	char const* email_domain    = 0;
	u4          split_threshold = 5 * 60;
	for (;;) {
		switch (getopt(argc, argv, "e:f:s:t:v")) {
			case -1: goto done_opt;

			case 'e': email_domain = optarg; break;

			case 'f':
				if (streq(optarg, "git")) {
					output_format = OUT_GIT;
				} else if (streq(optarg, "svn")) {
					output_format = OUT_SVN;
				} else {
					cerr << "error: unknown output format '" << optarg << "'\n";
					return EXIT_FAILURE;
				}
				break;

			case 's': {
				char* end;
				split_threshold = strtol(optarg, &end, 10);
				if (optarg == end) {
					cerr << "error: split threshold '" << optarg << "' is not a number\n";
					return EXIT_FAILURE;
				}
				switch (*end) {
					case '\0': break;

					case 'd': split_threshold *= 24;
					case 'h': split_threshold *= 60;
					case 'm': split_threshold *= 60;
					case 's':
						++end;
						if (*end != '\0') {
					default:
							cerr << "error: split threshold '" << optarg << "' has invalid suffix\n";
							return EXIT_FAILURE;
						}
				}
				break;
			}

			case 't': trunk_name = check_trunk_name(optarg); break;

			case 'v': verbose = true; break;

			case '?': return EXIT_FAILURE;
		}
	}
done_opt:

	argc -= optind;
	argv += optind;

	switch (output_format) {
		case OUT_GIT:
			if (!email_domain) email_domain = "invalid";
			if (!trunk_name)   trunk_name   = "master";
			break;

		case OUT_SVN:
			if (email_domain) {
				cerr << "error: -e is not valid for svn output\n";
				return EXIT_FAILURE;
			}
			if (!trunk_name) trunk_name = "trunk";
			break;
	}

	if (argc == 0) return EXIT_FAILURE;

	Sym::Exp      = Lexer::add_keyword("Exp");
	Sym::access   = Lexer::add_keyword("access");
	Sym::author   = Lexer::add_keyword("author");
	Sym::branch   = Lexer::add_keyword("branch");
	Sym::branches = Lexer::add_keyword("branches");
	Sym::comment  = Lexer::add_keyword("comment");
	Sym::date     = Lexer::add_keyword("date");
	Sym::dead     = Lexer::add_keyword("dead");
	Sym::desc     = Lexer::add_keyword("desc");
	Sym::expand   = Lexer::add_keyword("expand");
	Sym::head     = Lexer::add_keyword("head");
	Sym::locks    = Lexer::add_keyword("locks");
	Sym::log      = Lexer::add_keyword("log");
	Sym::next     = Lexer::add_keyword("next");
	Sym::state    = Lexer::add_keyword("state");
	Sym::strict   = Lexer::add_keyword("strict");
	Sym::symbols  = Lexer::add_keyword("symbols");
	Sym::text     = Lexer::add_keyword("text");

	FTS* const fts = fts_open(argv, FTS_PHYSICAL, compar);
	if (!fts) throw std::runtime_error("fts_open failed");

	u4 mark = 0;

	Indent     indent;
	Directory* curdir = new Directory();
	while (FTSENT* const ent = fts_read(fts)) {
		switch (ent->fts_info) {
			case FTS_D: {
				if (in_attic) {
					cerr << "error: Attic at " << *curdir << " has subdirectory\n";
				}
				if (ent->fts_name[0] == '\0') continue;
				if (streq(ent->fts_name, ATTIC)) {
					in_attic = true;
					continue;
				}
				if (verbose) cerr << indent << ent->fts_name << "/\n";
				++indent;
				curdir = new Directory(ent->fts_name, curdir);
				break;
			}

			case FTS_DP:
				if (ent->fts_name[0] == '\0') continue;
				if (streq(ent->fts_name, ATTIC)) {
					in_attic = false;
					continue;
				}
				--indent;
				curdir = curdir->parent;
				break;

			case FTS_F: {
				if (ent->fts_namelen < 2 || !streq(ent->fts_name + ent->fts_namelen - 2, ",v")) {
					cerr << CLEAR "warning: encountered non-RCS file " << *curdir << ent->fts_name << '\n';
					continue;
				}

				FILE* const file = fopen(ent->fts_accpath, "rb");
				if (!file) throw std::runtime_error("open failed");

				char* const suffix = &ent->fts_name[ent->fts_namelen - 2];
				*suffix = '\0';
				if (verbose) cerr << indent << ent->fts_name << endl;
				File* const f = new File(ent->fts_name, curdir, is_executable(file));
				*suffix = ',';

				++n_files;
				read_file(file, f);
				fclose(file);

				FileRev* r = f->head;

				if (FileRev const* next = f->head->next) {
					while (next->next) next = next->next;
					cerr << CLEAR "warning: head of " << *f << " is " << *r->rev << " but latest revision is " << *next->rev << '\n';
				}

				switch (output_format) {
					case OUT_GIT: {
						PieceTable p(*r->text);
						for (;;) {
							if (r->state != STATE_DEAD) {
								r->mark = ++mark;
#ifdef DEBUG_EXPORT
								cout << "# " << *f << ' ' << *r->rev << '\n';
#endif
								cout << "blob\n";
								cout << "mark :" << mark << '\n';
								cout << "data " << p.size() << '\n';
								cout << p << '\n';
							}
							if (!(r = r->pred)) break;
							p.modify(p, *r->text);
						}
						break;
					}

					case OUT_SVN: {
						r->content.set(*r->text);
						for (;;) {
							PieceTable const& c = r->content;
							if (!(r = r->pred)) break;
							r->content.modify(c, *r->text);
						}
						break;
					}
				}
				break;
			}
		}
	}
	print_read_status() << '\n';

	Vector<Changeset*> sets;
	for (Set<Changeset*>::iterator i = changesets.begin(), end = changesets.end(); i != end; ++i) {
		sets.push_back(*i);
	}

	std::sort(sets.begin(), sets.end(), older_changeset);

	Vector<Changeset*> splitsets;
	size_t k = 0;
	for (Vector<Changeset*>::const_iterator i = sets.begin(), end = sets.end(); i != end; ++i) {
		Changeset* const  c = *i;
#if DEBUG_SPLIT
		Date       const& o = c->oldest;
		Date       const& n = c->newest;
		cerr << endl << c << ' ' << o << ' ' << n << ' ' << *c->author << ' ' << c->filerevs.size() << (o != n ? " !!!" : "") << endl;
		cerr << "---\n" << *c->log << "\n---\n";
#endif

		Vector<FileRev*>::iterator const rbegin = c->filerevs.begin();
		Vector<FileRev*>::iterator const rend   = c->filerevs.end();

		std::sort(rbegin, rend, older_filerev);
		Set<File const*> contains;

		bool need_split = false;
		u4   last       = (*rbegin)->date.seconds();
		for (Vector<FileRev*>::const_iterator i = rbegin; i != rend; ++i) {
			FileRev const& f   = **i;
			u4      const  now = f.date.seconds();
			if (now - last > split_threshold || contains.find(f.file)) {
				need_split = true;
#if DEBUG_SPLIT
				contains.clear();
				cerr << "--- split ---\n";
#else
				break;
#endif
			}
			contains.insert(f.file);
			last = now;
#if DEBUG_SPLIT
			cerr << "  " << f.date << ' ' << f.state << ' ' << *f.rev;
			if (FileRev const* pred = f.pred)
				cerr << " <- " << *pred->rev;
			cerr << ' ' << *f.file << endl;
#endif
		}

		if (need_split) {
			u4         last   = (*rbegin)->date.seconds();
			Changeset* newset = new Changeset(c->log, c->author);
			contains.clear();
			for (Vector<FileRev*>::const_iterator i = rbegin; i != rend; ++i) {
				FileRev& f   = **i;
				u4 const now = f.date.seconds();
				if (now - last > split_threshold || contains.find(f.file)) {
					contains.clear();
					splitsets.push_back(newset);
					newset = new Changeset(c->log, c->author);
				}
				contains.insert(f.file);
				last = now;
				newset->add(&f);
			}
			splitsets.push_back(newset);
			delete c;
		} else {
			splitsets.push_back(c);
		}

		if (++k % 1000 == 0) {
			cerr << CLEAR "splitting... " << k << " -> " << splitsets.size();
		}
	}
	cerr << CLEAR "splitting... " << k << " -> " << splitsets.size() << '\n';

	Vector<Changeset*>::const_iterator const begin = splitsets.begin();
	Vector<Changeset*>::const_iterator const end   = splitsets.end();
	for (Vector<Changeset*>::const_iterator i = begin; i != end; ++i) {
		Changeset const& c = **i;

		for (Vector<FileRev*>::const_iterator i = c.filerevs.begin(), end = c.filerevs.end(); i != end; ++i) {
			FileRev const& f = **i;
			if (f.pred)
				++f.pred->changeset->n_succ;
		}
	}

	Heap<Changeset*, bool(Changeset const*, Changeset const*)> roots(older_changeset);
	for (Vector<Changeset*>::const_iterator i = begin; i != end; ++i) {
		Changeset* const c = *i;
		if (c->n_succ != 0) continue;
		roots.push(c);
	}

	Vector<Changeset const*> sorted;

	{
#if DEBUG_SPLIT
		cerr << "\nsorted:\n";
#endif
		size_t n = 0;
		while (!roots.empty()) {
			Changeset const& c = *roots.front();
			roots.pop();

			sorted.push_back(&c);

#if DEBUG_SPLIT
			cerr << &c << ' ' << c.oldest << ' ' << c.newest << ' ' << *c.author << ' ' << c.filerevs.size() << endl;
#endif

			for (Vector<FileRev*>::const_iterator i = c.filerevs.begin(), end = c.filerevs.end(); i != end; ++i) {
				FileRev const&       f    = **i;
				FileRev const* const pred = f.pred;
				if (!pred) continue;
				Changeset* const predc = pred->changeset;
				if (--predc->n_succ != 0) continue;
				roots.push(predc);
			}

			if (++n % 1000 == 0) {
				cerr << CLEAR "sorting... " << n;
			}
		}
		cerr << CLEAR "sorting... " << n << '\n';
	}

#if DEBUG_SPLIT
	{
		bool good = true;
		for (Vector<Changeset*>::const_iterator i = begin; i != end; ++i) {
			Changeset const& c = **i;
			if (c.n_succ == 0) continue;

			if (good) cerr << "\nmissing:\n";

			cerr << &c << ' ' << c.oldest << ' ' << c.newest << ' ' << *c.author << " files: " << c.filerevs.size() << " succ: " << c.n_succ << '\n';

			for (Vector<FileRev*>::const_iterator i = c.filerevs.begin(), end = c.filerevs.end(); i != end; ++i) {
				FileRev const& r = **i;
				cerr << "  " << *r.file << ' ' << *r.rev << '\n';
			}

			good = false;
		}
		if (!good) return EXIT_FAILURE;
	}
#endif

	{
		using std::setw;

		if (output_format == OUT_SVN) {
			cout << "SVN-fs-dump-format-version: 2\n\n";

			Date const& d = (*sorted.begin())->oldest;
			cout <<
				"Revision-number: 1\n"
				"Prop-content-length: 125\n"
				"Content-length: 125\n"
				"\n"
				"K 8\nsvn:date\nV 27\n" << std::setfill('0')
					<< setw(4) << (u4)d.year   << '-'
					<< setw(2) << (u4)d.month  << '-'
					<< setw(2) << (u4)d.day    << 'T'
					<< setw(2) << (u4)d.hour   << ':'
					<< setw(2) << (u4)d.minute << ':'
					<< setw(2) << (u4)d.second << ".000000Z\n"
				"K 7\nsvn:log\nV 51\nStandard project directories initialized by cvscvt.\n"
				"PROPS-END\n"
				"\n"
				"Node-path: " << trunk_name << "\n"
				"Node-kind: dir\n"
				"Node-action: add\n"
				"\n";
		}

		u4 const date1970 = Date(1970, 1, 1, 0, 0, 0).seconds();
		size_t n_commits = 0;

		Vector<Changeset const*>::const_iterator const begin = sorted.begin();
		Vector<Changeset const*>::const_iterator const end   = sorted.end();
		for (Vector<Changeset const*>::const_iterator i = end; i != begin;) {
			Changeset const& c = **--i;
			uptr<Blob> log(convert_log(*c.log));
			switch (output_format) {
				case OUT_GIT: {
#ifdef DEBUG_EXPORT
					cout << "# " << c.oldest << '\n';
#endif
					cout << "commit refs/heads/" << trunk_name << '\n';
					cout << "committer " << *c.author << " <" << *c.author << "@" << email_domain << "> " << c.oldest.seconds() - date1970 << " +0000\n";
					cout << "data " << log->size << '\n';
					cout << *log << '\n';
					for (Vector<FileRev*>::const_iterator i = c.filerevs.begin(), end = c.filerevs.end(); i != end; ++i) {
						FileRev const& r = **i;
						File    const& f = *r.file;
						if (r.state == STATE_DEAD) {
							cout << "D " << f << '\n';
						} else {
							char const* const mode = f.executable ? "100755" : "100644";
							cout << "M " << mode << " :" << r.mark << ' ' << f << '\n';
						}
					}
					break;
				}

				case OUT_SVN: {
					cout << "Revision-number: " << (n_commits + 2) << '\n';

					Blob   const& a        = *c.author;
					Date   const& d        = c.oldest;
					Blob   const& l        = *log;
					size_t const  prop_len =
						5 + 11 + 2 + log2(a.size) + 1 + a.size + 1 + // svn:author
						4 +  9 + 5 + 28 +                            // svn:date
						4 +  7 + 2 + log2(l.size) + 1 + l.size + 1 + // svn:log
						11;                                          // PROPS-END

					cout << "Prop-content-length: " << prop_len << '\n';
					cout << "Content-length: "      << prop_len << "\n\n";
					cout << "K 10\nsvn:author\nV "  << a.size << '\n' << a << '\n';
					cout << "K 8\nsvn:date\nV 27\n" << std::setfill('0')
						<< setw(4) << (u4)d.year   << '-'
						<< setw(2) << (u4)d.month  << '-'
						<< setw(2) << (u4)d.day    << 'T'
						<< setw(2) << (u4)d.hour   << ':'
						<< setw(2) << (u4)d.minute << ':'
						<< setw(2) << (u4)d.second << ".000000Z\n";
					cout << "K 7\nsvn:log\nV " << l.size << '\n' << l << '\n';
					cout << "PROPS-END\n";

					for (Vector<FileRev*>::const_iterator i = c.filerevs.begin(), end = c.filerevs.end(); i != end; ++i) {
						FileRev const& r         = **i;
						File    const& f         = *r.file;
						bool    const  cur_dead  = r.state == STATE_DEAD;
						bool    const  pred_dead = !r.pred || r.pred->state == STATE_DEAD;

						if (pred_dead && !cur_dead) {
							add_dir_entry(f.dir);
						}

						if (!cur_dead) {
							cout << "Node-path: " << trunk_name << '/' << f << "\nNode-kind: file\n";
							if (pred_dead) {
								cout << "Node-action: add\n";
							} else {
								cout << "Node-action: change\n";
							}

							size_t const text_len = r.content.size();
							size_t       prop_len = 0;

							bool const x = f.executable;
							if (x) prop_len += 26;

							if (prop_len != 0) {
								prop_len += 10; // PROPS-END
								cout << "Prop-content-length: " << prop_len << '\n';
							}
							cout << "Text-content-length: " << text_len            << '\n';
							cout << "Content-length: "      << prop_len + text_len << "\n\n";

							if (prop_len != 0) {
								if (x) {
									cout << "K 14\nsvn:executable\nV 1\n*\n";
								}

								cout << "PROPS-END\n";
							}

							cout << r.content;
						} else if (!pred_dead) {
							cout << "Node-path: " << trunk_name << '/' << f << "\nNode-action: delete\n\n";
							del_dir_entry(f.dir);
						}
					}

					cout << '\n';
					break;
				}
			}

			if (++n_commits % 100 == 0) cerr << CLEAR "emitting changesets... " << n_commits << ' ' << c.oldest;
		}
		cerr << CLEAR "emitting changesets... " << n_commits << '\n';
	}

	fts_close(fts);

	return EXIT_SUCCESS;
}
catch (std::exception const& e)
{
	cerr << CLEAR "error: " << e.what() << endl;
	return EXIT_FAILURE;
}
catch (...)
{
	cerr << CLEAR "error: caught unknown exception" << endl;
	return EXIT_FAILURE;
}
