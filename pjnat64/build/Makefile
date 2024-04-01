
include ../../build.mak
include ../../version.mak
include $(PJDIR)/build/common.mak

export LIBDIR := ../lib
export BINDIR := ../bin

RULES_MAK := $(PJDIR)/build/rules.mak

PJLIB_LIB:=../../pjlib/lib/libpj-$(TARGET_NAME)$(LIBEXT)
PJSIP_LIB:=../../pjsip/lib/libpjsip-$(TARGET_NAME)$(LIBEXT)
PJMEDIA_LIB:=../../pjmedia/lib/libpjmedia-$(TARGET_NAME)$(LIBEXT)
#PJSIP_SIMPLE_LIB := 
#PJSIP_UA_LIB := 
#PJSUA_LIB_LIB := 
#PJSUA2_LIB_LIB := 

export PJNAT64_LIB:=libpjnat64-$(TARGET_NAME)$(LIBEXT)

ifeq ($(PJ_SHARED_LIBRARIES),)
else
export PJNAT64_SONAME := libpjnat64.$(SHLIB_SUFFIX)
export PJNAT64_SHLIB := $(PJNAT64_SONAME).$(PJ_VERSION_MAJOR)
endif

###############################################################################
# Gather all flags.
#
export _CFLAGS 	:= $(CC_CFLAGS) $(OS_CFLAGS) $(HOST_CFLAGS) $(M_CFLAGS) \
		   $(CFLAGS) $(CC_INC)../include \
		   $(CC_INC)../../pjlib/include \
		   $(CC_INC)../../pjsip/include \
		   $(CC_INC)../../pjlib-util/include \
		   $(CC_INC)../../pjnath/include \
		   $(CC_INC)../../pjmedia/include
export _CXXFLAGS:= $(_CFLAGS) $(CC_CXXFLAGS) $(OS_CXXFLAGS) $(M_CXXFLAGS) \
		   $(HOST_CXXFLAGS) $(CXXFLAGS)
export _LDFLAGS := $(CC_LDFLAGS) $(OS_LDFLAGS) $(M_LDFLAGS) $(HOST_LDFLAGS) \
		   $(APP_LDFLAGS) $(LDFLAGS)

###############################################################################
# Defines for building PJNAT64 library
#
export PJNAT64_SRCDIR = ../src/pjnat64
export PJNAT64_OBJS += $(OS_OBJS) $(M_OBJS) $(CC_OBJS) $(HOST_OBJS) \
		pj-nat64.o
export PJNAT64_CFLAGS += $(_CFLAGS)
export PJNAT64_CXXFLAGS += $(_CXXFLAGS)
export PJNAT64_LDFLAGS += $(PJLIB_LDLIB) $(PJSIP_LDLIB) $(PJMEDIA_LDLIB) $(_LDFLAGS)

###############################################################################
# Main entry

TARGETS := $(PJNAT64_LIB) $(PJNAT64_SONAME)

all: $(TARGETS)

lib: $(TARGETS)

doc:

dep: depend
distclean: realclean

.PHONY: all dep depend clean realclean distclean
.PHONY: $(TARGETS)
.PHONY: $(PJNAT64_LIB) $(PJNAT64_SONAME)

pjnat64: $(PJNAT64_LIB)
$(PJNAT64_SONAME): $(PJNAT64_LIB)
$(PJNAT64_LIB) $(PJNAT64_SONAME): $(PJLIB_LIB) $(PJLIB_SONAME) $(PJSIP_LIB) $(PJSIP_SONAME) $(PJMEDIA_LIB) $(PJMEDIA_SONAME)
	$(MAKE) -f $(RULES_MAK) APP=PJNAT64 app=pjnat64 $(subst /,$(HOST_PSEP),$(LIBDIR)/$@)

.PHONY: pjnat64.ko
pjnat64.ko:
	echo Making $@
	$(MAKE) -f $(RULES_MAK) APP=PJNAT64 app=pjnat64 $(subst /,$(HOST_PSEP),$(LIBDIR)/$@)

clean:
	$(MAKE) -f $(RULES_MAK) APP=PJNAT64 app=pjnat64 $@

realclean:
	$(subst @@,$(subst /,$(HOST_PSEP),.pjnat64-$(TARGET_NAME).depend),$(HOST_RMR))
	$(MAKE) -f $(RULES_MAK) APP=PJNAT64 app=pjnat64 $@

depend:
	$(MAKE) -f $(RULES_MAK) APP=PJNAT64 app=pjnat64 $@
