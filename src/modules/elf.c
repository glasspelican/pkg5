/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <elf.h>
#include <gelf.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <port.h>
#include <unistd.h>

#include "Python.h"
#include "liblist.h"
#include "elfextract.h"

static void
pythonify_ver_liblist_cb(libnode_t *n, void *info, void *info2)
{
	PyObject *pverlist = (PyObject*)info;
	PyObject *ent;
	dyninfo_t *dyn = (dyninfo_t*)info2;

	ent = Py_BuildValue("s", elf_strptr(dyn->elf, dyn->dynstr, n->nameoff));

	PyList_Append(pverlist, ent);
}

static void
pythonify_2dliblist_cb(libnode_t *n, void *info, void *info2)
{
	PyObject *pdep = (PyObject*)info;
	dyninfo_t *dyn = (dyninfo_t*)info2;
	
	PyObject *pverlist;

	pverlist = PyList_New(0);
	liblist_foreach(n->verlist, pythonify_ver_liblist_cb, pverlist, dyn);
	PyList_Append(pdep, Py_BuildValue("[s,O]",
		elf_strptr(dyn->elf, dyn->dynstr, n->nameoff), pverlist));
}

static void
pythonify_1dliblist_cb(libnode_t *n, void *info, void *info2)
{
	PyObject *pdef = (PyObject*)info;
	dyninfo_t *dyn = (dyninfo_t*)info2;
	
	PyList_Append(pdef, Py_BuildValue("s", 
		elf_strptr(dyn->elf, dyn->dynstr, n->nameoff)));
}
/*
 * Open a file named by python, setting an appropriate error on failure.
 */
int
py_get_fd(PyObject *args)
{
	int fd;
	char *f;
	
	if (PyArg_ParseTuple(args, "s", &f) == 0) {
		PyErr_SetString(PyExc_ValueError, "could not parse argument");
		return (-1);
	}

	if ((fd = open(f, O_RDONLY)) < 0) {
		PyErr_SetFromErrnoWithFilename(PyExc_OSError, f);
		return (-1);
	}

	return (fd);
}

/*
 * For ELF operations: Need to check if a file is an ELF object.
 */
PyObject *
elf_is_elf_object(PyObject *self, PyObject *args)
{
	int fd, ret;

	if ((fd = py_get_fd(args)) < 0)
		return (NULL);

	ret = iself(fd);

	close(fd);

	return (Py_BuildValue("i", ret));
}

/*
 * Returns information about the ELF file in a dictionary
 * of the following format:
 *
 *  { 	
 *  	type: exe|so|core|rel, 
 *  	bits: 32|64, 
 *  	arch: sparc|x86|ppc|other|none,
 *  	end: lsb|msb,
 *  	osabi: none|linux|solaris|other
 *  }
 *
 *  XXX: I have yet to find a binary with osabi set to something
 *  aside from "none."
 */
PyObject *
get_info(PyObject *self, PyObject *args)
{
	int fd;
	int type = 0, bits = 0, arch = 0, data = 0;
	hdrinfo_t *hi = NULL;
	PyObject *pdict = NULL;
	
	if ((fd = py_get_fd(args)) < 0)
		return (NULL);

	if (!(hi = getheaderinfo(fd))) {
		PyErr_SetString(PyExc_RuntimeError, "could not get elf header");
		close(fd);
		return (NULL);
	}

	pdict = PyDict_New();
	PyDict_SetItemString(pdict, "type", 
	    Py_BuildValue("s", pkg_string_from_type(hi->type)));
	PyDict_SetItemString(pdict, "bits", Py_BuildValue("i", hi->bits));
	PyDict_SetItemString(pdict, "arch",
	    Py_BuildValue("s", pkg_string_from_arch(hi->arch)));
	PyDict_SetItemString(pdict, "end",
	    Py_BuildValue("s", pkg_string_from_data(hi->data)));
	PyDict_SetItemString(pdict, "osabi",
	    Py_BuildValue("s", pkg_string_from_osabi(hi->osabi)));

	free(hi);
	close(fd);
	return (pdict);
}

/*
 * Returns a dictionary with the relevant information.  No longer 
 * accurately titled "get_dynamic," as it returns the hash as well.
 *
 * The hash is currently of the following sections (when present):
 * 		.text .data .data1 .rodata .rodata1
 *
 * Dictionary format:
 * 
 * {
 *	runpath: "/path:/entries",
 *	defs: ["version", ... ],
 *	deps: [["file", ["versionlist"]], ...],
 * 	hash: "sha1hash"
 * }
 *
 * XXX: Currently, defs contains some duplicate entries.  There 
 * may be meaning attached to this, or it may just be something 
 * worth trimming out at this stage or above.
 * 
 */
PyObject *
get_dynamic(PyObject *self, PyObject *args)
{
	int 	fd;
	dyninfo_t 	*dyn = NULL;
	PyObject	*pdep = NULL;
	PyObject	*pdef = NULL;
	PyObject	*pdict = NULL;

	if ((fd = py_get_fd(args)) < 0)
		return (NULL);

	dyn = getdynamic(fd);

	if (!dyn) {
		PyErr_SetString(PyExc_RuntimeError,
		    "failed to load dynamic section");
		return (NULL);
	}
	
	pdep = PyList_New(0);
	liblist_foreach(dyn->deps, pythonify_2dliblist_cb, pdep, dyn);
	pdef = PyList_New(0);
	liblist_foreach(dyn->defs, pythonify_1dliblist_cb, pdef, dyn);

	pdict = PyDict_New();
	PyDict_SetItemString(pdict, "runpath",
	    Py_BuildValue("s",elf_strptr(dyn->elf, dyn->dynstr, dyn->runpath)));
	PyDict_SetItemString(pdict, "deps", pdep);
	PyDict_SetItemString(pdict, "defs", pdef);
	PyDict_SetItemString(pdict, "hash", Py_BuildValue("s", dyn->hash));
	
	dyninfo_free(dyn);
	close(fd);

	return (pdict);
}

/*
 * XXX: Implemented as part of get_dynamic above.
 * 
 * For ELF nontriviality: Need to turn an ELF object into a unique hash.
 *
 * From Eric Saxe's investigations, we see that the following sections can
 * generally be ignored:
 *
 *    .SUNW_signature, .comment, .SUNW_ctf, .debug, .plt, .rela.bss, .rela.plt,
 *    .line, .note
 *
 * Conversely, the following sections are generally significant:
 *
 *    .rodata.str1.8, .rodata.str1.1, .rodata, .data1, .data, .text
 *
 * Accordingly, we will hash on the latter group of sections to determine our
 * ELF hash.
 */


static PyMethodDef methods[] = {
	{ "is_elf_object", elf_is_elf_object, METH_VARARGS },
	{ "get_info", get_info, METH_VARARGS },
	{ "get_dynamic", get_dynamic, METH_VARARGS },
	{ NULL, NULL }
};

void initelf() {
	Py_InitModule("elf", methods);
}
