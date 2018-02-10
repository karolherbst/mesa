include Makefile.sources

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/winsys

if HAVE_CLOVER_ICD
AM_CPPFLAGS += -DHAVE_CLOVER_ICD
else
# Only install the headers if we are building a stand-alone implementation
cldir = $(includedir)/CL
cl_HEADERS = \
	$(top_srcdir)/include/CL/cl.h \
	$(top_srcdir)/include/CL/cl.hpp \
	$(top_srcdir)/include/CL/cl_d3d10.h \
	$(top_srcdir)/include/CL/cl_d3d11.h \
	$(top_srcdir)/include/CL/cl_dx9_media_sharing.h \
	$(top_srcdir)/include/CL/cl_egl.h \
	$(top_srcdir)/include/CL/cl_ext.h \
	$(top_srcdir)/include/CL/cl_gl.h \
	$(top_srcdir)/include/CL/cl_gl_ext.h \
	$(top_srcdir)/include/CL/cl_platform.h \
	$(top_srcdir)/include/CL/opencl.h
endif

noinst_LTLIBRARIES = libclover.la libclllvm.la libclspirv.la

libclllvm_la_CXXFLAGS = \
	$(CXX11_CXXFLAGS) \
	$(VISIBILITY_CXXFLAGS) \
	$(LLVM_CXXFLAGS) \
	$(CLOVER_STD_OVERRIDE) \
	$(DEFINES) \
	$(LIBELF_CFLAGS) \
	-DLIBCLC_INCLUDEDIR=\"$(LIBCLC_INCLUDEDIR)/\" \
	-DLIBCLC_LIBEXECDIR=\"$(LIBCLC_LIBEXECDIR)/\" \
	-DCLANG_RESOURCE_DIR=\"$(CLANG_RESOURCE_DIR)\" \
	$(LLVMSPIRVLIB_CFLAGS)

libclllvm_la_SOURCES = $(LLVM_SOURCES)

libclllvm_la_LDFLAGS = \
	$(LLVMSPIRVLIB_LIBS)

libclspirv_la_CXXFLAGS = \
	$(CXX11_CXXFLAGS) \
	$(CLOVER_STD_OVERRIDE) \
	$(VISIBILITY_CXXFLAGS) \
	$(SPIRV_TOOLS_CFLAGS)

libclspirv_la_SOURCES = $(SPIRV_SOURCES)

libclspirv_la_LDFLAGS = \
	$(SPIRV_TOOLS_LIBS)

libclover_la_CXXFLAGS = \
	$(CXX11_CXXFLAGS) \
	$(CLOVER_STD_OVERRIDE) \
	$(VISIBILITY_CXXFLAGS)

libclover_la_LIBADD = \
	libclllvm.la libclspirv.la

libclover_la_SOURCES = $(CPP_SOURCES)

EXTRA_DIST = Doxyfile meson.build
