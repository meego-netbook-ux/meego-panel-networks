SUBDIRS = carrick src data tests po build

ACLOCAL_AMFLAGS = -I build/autotools

-include $(top_srcdir)/git.mk

MAINTAINERCLEANFILES = \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/compile \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/mkinstalldirs \
	$(srcdir)/omf.make \
	$(srcdir)/xmldocs.make \
	$(srcdir)/build/autotools/*.m4 \
	`find "$(srcdir)" -type f -name Makefile.in -print`
