# Sensel requires Sensel API, so the user needs to download it from https://github.com/sensel/sensel-api
# Next go to sensel-install inside the downloaded git repository and install the OSX pkg file
# Only then will this build cleanly

sensel.class.sources = sensel.c
sensel.class.ldlibs = -L./ -lsensel

define forWindows
    sensel.class.ldlibs = -L/C/Program\ Files/Sensel/SenselLib/x86 -lsensel
    CPPFLAGS += -I./sensel-win-msys-include
endef

datafiles = sensel-help.pd

include Makefile.pdlibbuilder.revised
