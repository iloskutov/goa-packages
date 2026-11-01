/*
 * \brief  glmark2's 'SharedLibrary' for Mesa 26
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Replacement of glmark2's 'shared-library.cpp'. glmark2 resolves all EGL
 * and GL entry points at run time, and the Genode port
 * patches the library names of the stock Mesa into it ("egl.lib.so",
 * "mesa.lib.so"). This version opens the libraries of Mesa 26 for these two
 * names, so that the port's sources can be used unchanged.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* libc includes */
#include <dlfcn.h>
#include <string.h>

/* glmark2 includes */
#include <shared-library.h>


static char const *mesa26_name(char const *name)
{
	if (!strcmp(name, "egl.lib.so"))  return "egl26.lib.so";
	if (!strcmp(name, "mesa.lib.so")) return "mesa26.lib.so";

	return name;
}


SharedLibrary::SharedLibrary() : handle_(nullptr) { }


SharedLibrary::~SharedLibrary() { close(); }


bool SharedLibrary::open(char const *name)
{
	/* lazy binding as in the port's 'dlopen.patch' */
	handle_ = dlopen(mesa26_name(name), RTLD_LAZY | RTLD_NODELETE);

	return handle_ != nullptr;
}


bool SharedLibrary::open_from_alternatives(std::initializer_list<char const *> alt_names)
{
	for (char const *name : alt_names)
		if (open(name))
			return true;

	return false;
}


void SharedLibrary::close()
{
	if (handle_)
		dlclose(handle_);
}


void *SharedLibrary::handle() const { return handle_; }


void *SharedLibrary::load(char const *symbol) const
{
	return dlsym(handle_, symbol);
}
