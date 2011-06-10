CFG ?= default
-include config.$(CFG)

PROG     ?= cvscvt
BUILDDIR ?= build/$(CFG)

CFLAGS += -Wall -W

SRCS :=
SRCS += date.cc
SRCS += indent.cc
SRCS += lexer.cc
SRCS += main.cc
SRCS += piecetable.cc

Q ?= @

DEPS := $(patsubst %, $(BUILDDIR)/%.d, $(basename $(SRCS)))
OBJS := $(patsubst %, $(BUILDDIR)/%.o, $(basename $(SRCS)))
DIRS := $(sort $(dir $(OBJS)))

# Make build directories
DUMMY := $(shell mkdir -p $(DIRS))

all: $(BUILDDIR)/$(PROG)

-include $(DEPS)

$(BUILDDIR)/$(PROG): $(OBJS)
	@echo "===> LD  $@"
	$(Q)$(CXX) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(BUILDDIR)/%.o: %.cc
	@echo "===> CXX $<"
	$(Q)$(CXX) $(CFLAGS) -MMD -c -o $@ $<

clean:
	@echo "===> CLEAN"
	$(Q)rm -fr $(BUILDDIR)
