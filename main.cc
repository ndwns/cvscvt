#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
#include "types.h"
#include "vector.h"

#define CLEAR "\r\x1B[K"

#ifndef DEBUG_SPLIT
#	define DEBUG_SPLIT 0
#endif

using std::cerr;
using std::cout;
using std::endl;

namespace Sym
{
	static Symbol access;
	static Symbol author;
	static Symbol branch;
	static Symbol branches;
	static Symbol comment;
	static Symbol date;
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
		if ('1' <= *i && *i <= '9') {
			major = *i++ - '0';
			for (;;) {
				if (i == end) goto invalid;
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

		u4 minor;
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
	Directory(char const* const name, Directory const* const parent) :
		name(strdup(name)),
		parent(parent)
	{}

	char*            name;
	Directory const* parent;
};

static std::ostream& operator <<(std::ostream& o, Directory const& d)
{
	if (d.parent) {
		o << *d.parent;
	}
	return o << d.name << '/';
}

struct FileRev;

struct File
{
	File(char const* const name, Directory const* const dir) :
		name(strdup(name)),
		dir(dir),
		head()
	{}

	u4 hash() const { return (uintptr_t)this >> 4; }

	char*            const name;
	Directory const* const dir;
	FileRev*               head;
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
		changeset(),
		mark()
	{}

	u4 hash() const { return rev->hash(); }

	File   const* file;
	RevNum const* rev;
	Date          date;
	Symbol        author;
	Symbol        state;
	Symbol        log;
	Symbol        text;
	FileRev*      pred;
	Changeset*    changeset;
	u4            mark;
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

static bool            verbose = false;
static Set<Changeset*> changesets;
static size_t          file_revs;
static size_t          on_trunk;
static size_t          n_files;

static std::ostream& print_read_status()
{
	return cerr << CLEAR << n_files << " files, " << file_revs << " file revisions, " << on_trunk << " on trunk, " << changesets.size() << " changesets";
}

static void read_file(char const* const filename, File* const file)
{
	FILE* const f = fopen(filename, "rb");
	if (!f) throw std::runtime_error("open failed");
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
				RevNum const* const pred = RevNum::parse(snext);
				filerev->pred = revs.insert(new FileRev(file, pred));
			}
			filerev->date   = date;
			filerev->author = sauthor;
			filerev->state  = sstate;
		}
	}

	l.expect(Sym::desc);
	l.expect(T_STRING);

	while (Symbol const srev = l.accept(T_NUM)) {
		l.expect(Sym::log);
		Symbol const slog = l.expect(T_STRING);
		l.expect(Sym::text);
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

	fclose(f);
}

#ifdef __APPLE__
typedef FTSENT const**       FTSENT_cmp;
#else
typedef FTSENT const* const* FTSENT_cmp;
#endif

static int compar(FTSENT_cmp const a, FTSENT_cmp const b)
{
	return strcmp((*a)->fts_name, (*b)->fts_name);
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

int main(int argc, char** argv)
try
{
	u4 split_threshold = 5 * 60;
	for (;;) {
		switch (getopt(argc, argv, "s:v")) {
			case -1: goto done_opt;

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

			case 'v': verbose = true; break;

			case '?': return EXIT_FAILURE;
		}
	}
done_opt:

	argc -= optind;
	argv += optind;

	if (argc == 0) return EXIT_FAILURE;

	Sym::access   = Lexer::add_keyword("access");
	Sym::author   = Lexer::add_keyword("author");
	Sym::branch   = Lexer::add_keyword("branch");
	Sym::branches = Lexer::add_keyword("branches");
	Sym::comment  = Lexer::add_keyword("comment");
	Sym::date     = Lexer::add_keyword("date");
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

	Symbol const dead = Lexer::add_keyword("dead");

	FTS* const fts = fts_open(argv, FTS_PHYSICAL, compar);
	if (!fts) throw std::runtime_error("fts_open failed");

	u4 mark = 0;

	Indent           indent;
	Directory const* curdir = 0;
	while (FTSENT* const ent = fts_read(fts)) {
		switch (ent->fts_info) {
			case FTS_D: {
				if (ent->fts_name[0] == '\0')            continue;
				if (strcmp(ent->fts_name, "Attic") == 0) continue;
				if (verbose) cerr << indent << ent->fts_name << "/\n";
				++indent;
				curdir = new Directory(ent->fts_name, curdir);
				break;
			}

			case FTS_DP:
				if (ent->fts_name[0] == '\0')            continue;
				if (strcmp(ent->fts_name, "Attic") == 0) continue;
				--indent;
				curdir = curdir->parent;
				break;

			case FTS_F: {
				if (ent->fts_namelen < 2) continue;
				if (strcmp(ent->fts_name + ent->fts_namelen - 2, ",v") != 0) continue;

				char* const suffix = &ent->fts_name[ent->fts_namelen - 2];
				*suffix = '\0';
				if (verbose) cerr << indent << ent->fts_name << endl;
				File* const f = new File(ent->fts_name, curdir);
				*suffix = ',';

				++n_files;
				read_file(ent->fts_accpath, f);

				PieceTable p(*f->head->text);
				for (FileRev* i = f->head; i; i = i->pred) {
					if (i != f->head) {
						p.modify(*i->text);
					}
					if (i->state != dead) {
						i->mark = ++mark;
#ifdef DEBUG_EXPORT
						cout << "# " << *f << ' ' << *i->rev << '\n';
#endif
						cout << "blob\n";
						cout << "mark :" << mark << '\n';
						cout << "data " << p.size() << '\n';
						cout << p << '\n';
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

	cerr << "splitting...\n";

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
			cerr << "  " << f.date << ' ' << *f.state << ' ' << *f.rev;
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

		if (++k % 100 == 0) {
			cerr << CLEAR << k << " -> " << splitsets.size();
		}
	}
	cerr << CLEAR << k << " -> " << splitsets.size() << '\n';

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

	{ cerr << "sorting...\n";
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

			if (++n % 100 == 0) cerr << CLEAR << n;
		}
		cerr << CLEAR << n << '\n';
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

	{ cerr << "emitting changesets...\n";

		u4 const date1970 = Date(1970, 1, 1, 0, 0, 0).seconds();
		size_t n = 0;

		Vector<Changeset const*>::const_iterator const begin = sorted.begin();
		Vector<Changeset const*>::const_iterator const end   = sorted.end();
		for (Vector<Changeset const*>::const_iterator i = end; i != begin;) {
			Changeset const& c = **--i;
#ifdef DEBUG_EXPORT
			cout << "# " << c.oldest << '\n';
#endif
			cout << "commit refs/heads/trunk\n";
			cout << "committer " << *c.author << " <" << *c.author << "@invalid> " << c.oldest.seconds() - date1970 << " +0000\n";
			cout << "data " << c.log->size << '\n';
			cout << *c.log << '\n';
			for (Vector<FileRev*>::const_iterator i = c.filerevs.begin(), end = c.filerevs.end(); i != end; ++i) {
				FileRev const& f = **i;
				if (f.state == dead) {
					cout << "D " << *f.file << '\n';
				} else {
					cout << "M 100644 :" << f.mark << ' ' << *f.file << '\n';
				}
			}

			if (++n % 100 == 0) cerr << CLEAR << n << ' ' << c.oldest;
		}
		cerr << CLEAR << n << '\n';
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
