# -*- mode: makefile-gmake -*-

os := $(shell uname -s)

all: macDict

src_files = src/macDict.cpp

ifeq ($(os),Darwin)
macDict: $(src_files)
	clang++ -o $@ -O3 -std=c++11 $(src_files) \
		-I/opt/local/include \
		-L/opt/local/lib \
		-lz -lxml2
endif

ifeq ($(os),Linux)

defines  = -DBits64_ -DLINUX -DNDEBUG
cxxflags = -O3 -m64 -std=c++11 -fPIC

packages = zlib libxml-2.0
includes := $(shell pkg-config --cflags $(packages))
ldflags  := $(shell pkg-config --libs $(packages))

cxx = g++

# set to 0 to build without GUI
want_gui = 1

obj_files =

depdir = build/deps
escape_slash=$(shell echo $(1) | sed -e 's/\//\\\//g')


ifeq ($(want_gui),1)

src_files += src/Window.cpp src/LineEdit.cpp

qtpackages = Qt5WebEngineWidgets Qt5Widgets Qt5Gui Qt5Core
includes += $(shell pkg-config --cflags $(qtpackages))
ldflags  += $(shell pkg-config --libs $(qtpackages))
defines  += -DWANT_GUI

moc = moc

moc_files := \
	$(patsubst src/%.h,build/moc/moc_%.cpp,\
	$(shell grep -l -r --include='*.h' Q_OBJECT src))
obj_files += $(patsubst build/%.cpp,build/obj/%.o,$(moc_files))

build/moc/moc_%.cpp: src/%.h
	/bin/mkdir -p $(@D)
	$(moc) $(defines) $(includes) $< -o $@
build/obj/moc/%.o : build/moc/%.cpp
	/bin/mkdir -p $(@D)
	$(cxx) $(defines) $(includes) $(cxxflags) -c -o $@ $<
$(depdir)/moc/%.d : build/moc/%.cpp
	mkdir -p $(@D)
	/bin/sh -c '$(cxx) -DGENERATE_DEPENDS -MM -MF $@ \
	-MT build/obj/moc/$*.o \
	-MT $(call escape_slash,$@) \
	$(defines) $(includes) $(cxxflags) $< || rm -f $@; [ -e $@ ]'

ifneq ($(MAKECMDGOALS),clean)
-include $(moc_files:build/moc/%.cpp=$(depdir)/moc/%.d)
endif


testgui: macDict
	./macDict.sh

endif # want_gui


obj_files += $(patsubst src/%.cpp,build/obj/%.o,$(src_files))

build/obj/%.o : src/%.cpp
	/bin/mkdir -p $(@D)
	$(cxx) $(defines) $(includes) $(cxxflags) -c -o $@ $<
$(depdir)/%.d : src/%.cpp
	mkdir -p $(@D)
	/bin/sh -c '$(cxx) -DGENERATE_DEPENDS -MM -MF $@ \
	-MT build/obj/$*.o \
	-MT $(call escape_slash,$@) \
	$(defines) $(includes) $(cxxflags) $< || rm -f $@; [ -e $@ ]'

ifneq ($(MAKECMDGOALS),clean)
-include $(src_files:src/%.cpp=$(depdir)/%.d)
endif

macDict: $(obj_files)
	$(cxx) -o $@ $(cxxflags) $(obj_files) $(ldflags)

endif # linux


test: macDict
	./macDict.sh -o /tmp/out.html callipygian

clean:
	rm -rf macDict build
