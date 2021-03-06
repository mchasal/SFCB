# $Id: Makefile.am,v 1.105 2010/02/10 20:51:15 buccella Exp $  
#
#  Makefile.am
# 
#   © Copyright IBM Corp. 2005, 2007
# 
#  THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
#  ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
#  CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
# 
#  You can obtain a current copy of the Eclipse Public License from
#  http://www.opensource.org/licenses/eclipse-1.0.php
# 
#  Author:        Viktor Mihajlovski <mihajlov@de.ibm.com>
#  Contributions: Adrian Schuur <schuur@de.ibm.com>
#                 Sven Schuetz <sven@de.ibm.com>
# 
#  Description:
# 
#  Makefile process input for sfcb.
# 
# 
AUTOMAKE_OPTIONS = no-dist-gzip dist-bzip2

sfcbdocdir=$(datadir)/doc/sfcb-$(VERSION)
sfcbdatadir=$(datadir)/sfcb
sfcbconfdir=$(sysconfdir)/sfcb
sfcbstatedir=$(localstatedir)/lib/sfcb
initdir=$(sysconfdir)/init.d
pamdir=$(sysconfdir)/pam.d
sfcblibdir=$(libdir)/sfcb
cmpilibdir=$(libdir)/cmpi
systemddir=$(prefix)/share/systemd

MANFILES=man/genSslCert.1 man/getSchema.1 man/sfcbd.1 man/sfcbmof.1 \
	man/sfcbrepos.1 man/sfcbstage.1 man/sfcbunstage.1 man/sfcbuuid.1 \
	man/wbemcat.1 man/xmltest.1

BUILT_SOURCES=queryParser.c queryLexer.c cimXmlOps.c cimXmlParserProcessed.c $(MANFILES)

AM_YFLAGS=-d
AM_CPPFLAGS=-DSFCB_CONFDIR=\"$(sfcbconfdir)\" \
	-DSFCB_STATEDIR=\"$(sfcbstatedir)\" \
	-DSFCB_LIBDIR=\"$(sfcblibdir)\" \
	-DCMPI_LIBDIR=\"$(cmpilibdir)\" \
	-DLIBDIR=\"$(libdir)\" \
	-DSFCB_BINARY=\"$(sbindir)/sfcbd\" \
	@SFCB_CPPFLAGS@ \
        @SFCB_CMPI_PLATFORM@ 
AM_LDFLAGS=@SFCB_LDFLAGS@

SUBDIRS=. $(MOFC_DIR) $(CMPIDEV_DIR) test

if GCOV
AM_CFLAGS=@SFCB_CFLAGS@ -ftest-coverage -fprofile-arcs
else
AM_CFLAGS=@SFCB_CFLAGS@
endif

if INDICATIONS
INDICATION_LIBS = libsfcIndCIMXMLHandler.la
else
INDICATION_LIBS =
endif

if LOCAL_CONNECT_ONLY
HTTP_ADAPTER_LIBS =
HTTP_ADAPTER_LIBS_LINK =
else
HTTP_ADAPTER_LIBS = libsfcHttpAdapter.la
HTTP_ADAPTER_LIBS_LINK = -lsfcHttpAdapter
endif

#
# if enable_local_connect_no_indication is set to "yes"
# then we won't build libsfcCimXmlCodec.so, libsfcHttpAdapter.so
# and libsfcIndCIMXMLHandler..so
#
if LOCAL_CONNECT_NO_INDICATION
CIMXMLCODEC_LIBS =
CIMXMLCODEC_LIBS_LINK =

HTTP_ADAPTER_LIBS =
HTTP_ADAPTER_LIBS_LINK =

INDICATION_LIBS =
else
CIMXMLCODEC_LIBS = libsfcCimXmlCodec.la
CIMXMLCODEC_LIBS_LINK = -lsfcCimXmlCodec

HTTP_ADAPTER_LIBS = libsfcHttpAdapter.la
HTTP_ADAPTER_LIBS_LINK = -lsfcHttpAdapter
endif

SFC_CUSTOM_LIB = libsfcCustomLib.la

if QUALREP
QUALREP_LIBS = libsfcQualifierProvider.la
QUALREP_FILES = qualifier.c
QUALREP_HEADER = qualifier.h
else
QUALREP_LIBS = 
QUALREP_FILES =
QUALREP_HEADER =
endif

if PAM
    PAM_LIBS = libsfcBasicPAMAuthentication.la 
else
    PAM_LIBS =
endif

if ACCOUNT_PASSTHRU
    ACCOUNT_PASSTHRU_LIBS = libsfccimAccountPassthroughProvider.la 
else
    ACCOUNT_PASSTHRU_LIBS = 
endif

if SLP
    PROFILE_PROVIDER_LIBS = libsfcProfileProvider.la
else
    PROFILE_PROVDIER_LIBS = 
endif

sfcb_sharedobjects = \
   $(sfcblibdir)/*.so

sfcblib_LTLIBRARIES = \
   libsfcBrokerCore.la \
   libsfcFileRepository.la \
   libsfcInternalProvider.la \
   libsfcInteropProvider.la \
   libsfcInteropServerProvider.la \
   $(PROFILE_PROVIDER_LIBS) \
   libsfcElementCapabilitiesProvider.la \
   libsfcClassProvider.la \
   libsfcClassProviderGz.la \
   libsfcClassProviderSf.la \
   libsfcClassProviderMem.la \
   $(CIMXMLCODEC_LIBS) \
   $(HTTP_ADAPTER_LIBS) \
   libsfcBasicAuthentication.la \
   libsfcCertificateAuthentication.la \
   libsfcObjectImplSwapI32toP32.la \
   libcimcClientSfcbLocal.la \
   $(QUALREP_LIBS) \
   $(INDICATION_LIBS) \
   $(PAM_LIBS) \
   $(SFC_CUSTOM_LIB) \
   $(ACCOUNT_PASSTHRU_LIBS) 

if TEST_ENABLED 
sfcblib_LTLIBRARIES += \
   libTestAssociationProvider.la \
   libTestMethodProvider.la \
   libTestInstanceProvider.la \
   libTestMiscProvider.la \
   libTestExecQueryProvider.la \
   libTestIndicationProvider.la
endif

sbin_PROGRAMS = \
   sfcbd 

bin_PROGRAMS = \
   sfcbmofpp sfcbdump sfcbinst2mof sfcbtrace

noinst_PROGRAMS = \
   sfcbdumpP32onI32 classSchema2c sfcbsem

libsfcBrokerCore_la_SOURCES = \
    args.c \
    array.c \
    brokerEnc.c \
    brokerUpc.c \
    brokerOs.c \
    context.c \
    datetime.c \
    enumeration.c \
    instance.c \
    objectpath.c \
    result.c \
    selectexp.c \
    selectcond.c \
    subcond.c \
    predicate.c \
    string.c \
    value.c \
    support.c \
    providerRegister.c \
    objectImpl.c \
    constClass.c \
    msgqueue.c \
    providerMgr.c \
    providerDrv.c \
    trace.c \
    control.c \
    queryParser.y \
    queryLexer.l \
    queryOperation.c \
    queryStatement.c \
    cimXmlGen.c \
    mrwlock.c \
    mlog.c \
    $(QUALREP_FILES)

libsfcBrokerCore_la_CFLAGS = $(AM_CFLAGS) @SFCB_CMPI_OS@ 

queryLexer.c: $(srcdir)/queryLexer.l
	$(LEX) -t $< | sed -e "s/yy/sfcQuery/g" > $@

queryParser.c: $(srcdir)/queryParser.y
	$(YACC) $(AM_YFLAGS) -p sfcQuery -o $@ $< 

cimXmlOps.c: $(srcdir)/cimXmlOps.y
	$(YACC) $(AM_YFLAGS) -p sfcXml -o $@ $< 

cimXmlParserProcessed.c: $(srcdir)/cimXmlParser.c
	sed -e "s/yy/sfcXml/g" $< > $@

libcimcClientSfcbLocal_la_SOURCES = \
   cimcClientSfcbLocal.c
libcimcClientSfcbLocal_la_LIBADD=-lsfcBrokerCore
libcimcClientSfcbLocal_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcFileRepository_la_SOURCES = \
	fileRepository.c
libsfcFileRepository_la_LIBADD=-lsfcBrokerCore	
libsfcFileRepository_la_DEPENDENCIES=libsfcBrokerCore.la

#
# if enable_local_connect_no_indication is not turned on then
# we will build libsfcHttpAdapter
#
if !LOCAL_CONNECT_NO_INDICATION
libsfcHttpAdapter_la_SOURCES = \
   httpAdapter.c \
   httpComm.c
libsfcHttpAdapter_la_LIBADD=-lsfcBrokerCore $(CIMXMLCODEC_LIBS_LINK)
libsfcHttpAdapter_la_DEPENDENCIES=libsfcBrokerCore.la $(CIMXMLCODEC_LIBS)
endif

libsfcCustomLib_la_SOURCES = \
   sfcCustomLib.c

libsfcBasicAuthentication_la_SOURCES = \
   sfcBasicAuthentication.c

if PAM
libsfcBasicPAMAuthentication_la_SOURCES = \
   sfcBasicPAMAuthentication.c
libsfcBasicPAMAuthentication_la_LIBADD=-lpam
endif

libsfcCertificateAuthentication_la_SOURCES = \
   sfcCertificateAuthentication.c

libsfcInternalProvider_la_SOURCES = \
   internalProvider.c
libsfcInternalProvider_la_LIBADD=-lsfcBrokerCore -lsfcFileRepository
libsfcInternalProvider_la_DEPENDENCIES=libsfcBrokerCore.la libsfcFileRepository.la

libsfcObjectImplSwapI32toP32_la_SOURCES = \
   objectImplSwapI32toP32.c 
libsfcObjectImplSwapI32toP32_la_LIBADD=-lsfcBrokerCore
libsfcObjectImplSwapI32toP32_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcInteropProvider_la_SOURCES = \
   interopProvider.c 
libsfcInteropProvider_la_LIBADD=-lsfcBrokerCore
libsfcInteropProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcInteropServerProvider_la_SOURCES = \
   interopServerProvider.c 
libsfcInteropServerProvider_la_LIBADD=-lsfcBrokerCore 
libsfcInteropServerProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcIndCIMXMLHandler_la_SOURCES = \
   indCIMXMLHandler.c \
   indCIMXMLExport.c 
libsfcIndCIMXMLHandler_la_LIBADD=-lsfcBrokerCore -lsfcInternalProvider $(CIMXMLCODEC_LIBS_LINK) $(HTTP_ADAPTER_LIBS_LINK)
libsfcIndCIMXMLHandler_la_DEPENDENCIES=libsfcBrokerCore.la libsfcInternalProvider.la \
	$(CIMXMLCODEC_LIBS) $(HTTP_ADAPTER_LIBS)

libsfcProfileProvider_la_SOURCES =  $(SLP_SRC)
libsfcProfileProvider_la_LIBADD=-lsfcBrokerCore 
libsfcProfileProvider_la_DEPENDENCIES=libsfcBrokerCore.la 

libsfcElementCapabilitiesProvider_la_SOURCES = \
   elementCapabilitiesProvider.c 
libsfcElementCapabilitiesProvider_la_LIBADD=-lsfcBrokerCore
libsfcElementCapabilitiesProvider_la_DEPENDENCIES=libsfcBrokerCore.la

if ACCOUNT_PASSTHRU
libsfccimAccountPassthroughProvider_la_SOURCES = cimAccountPassthroughProvider.c
libsfccimAccountPassthroughProvider_la_LIBADD=-lsfcBrokerCore
libsfccimAccountPassthroughProvider_la_DEPENDENCIES=libsfcBrokerCore.la
endif

libsfcClassProviderGz_la_SOURCES = \
   classProviderGz.c
libsfcClassProviderGz_la_LIBADD=-lsfcBrokerCore @SFCB_LIBZ@
libsfcClassProviderGz_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcClassProviderSf_la_SOURCES = \
   classProviderSf.c
libsfcClassProviderSf_la_LIBADD=-lsfcBrokerCore @SFCB_LIBZ@
libsfcClassProviderSf_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcClassProvider_la_SOURCES = \
   classProvider.c
libsfcClassProvider_la_LIBADD=-lsfcBrokerCore
libsfcClassProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libsfcClassProviderMem_la_SOURCES = \
   classProviderMem.c classSchemaMem.c interop_classSchema.c
libsfcClassProviderMem_la_LIBADD=-lsfcBrokerCore
libsfcClassProviderMem_la_DEPENDENCIES=libsfcBrokerCore.la

# Building providers in the test/TestProviders directory
if TEST_ENABLED
libTestAssociationProvider_la_SOURCES = \
   test/TestProviders/cmpiTestAssociationProvider.c
libTestAssociationProvider_la_LIBADD = -lsfcBrokerCore
libTestAssociationProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libTestMethodProvider_la_SOURCES = \
   test/TestProviders/cmpiTestMethodProvider.c
libTestMethodProvider_la_LIBADD = -lsfcBrokerCore
libTestMethodProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libTestInstanceProvider_la_SOURCES = \
   test/TestProviders/cmpiTestInstanceProvider.c
libTestInstanceProvider_la_LIBADD = -lsfcBrokerCore
libTestInstanceProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libTestMiscProvider_la_SOURCES = \
   test/TestProviders/cmpiTestMiscProvider.c
libTestMiscProvider_la_LIBADD = -lsfcBrokerCore
libTestMiscProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libTestExecQueryProvider_la_SOURCES = \
   test/TestProviders/cmpiTestExecQueryProvider.c
libTestExecQueryProvider_la_LIBADD = -lsfcBrokerCore
libTestExecQueryProvider_la_DEPENDENCIES=libsfcBrokerCore.la

libTestIndicationProvider_la_SOURCES = \
   test/TestProviders/cmpiTestIndicationProvider.c
libTestIndicationProvider_la_LIBADD = -lsfcBrokerCore
libTestIndicationProvider_la_DEPENDENCIES=libsfcBrokerCore.la
endif

# if we enable CIMrs requests, then compile in the CIMrs
# request processor
if CIMRS
CIMRS_PROCESSOR = cimRsRequest.c
else
CIMRS_PROCESSOR = 
endif

#
# if enable_local_connect_no_indication is not turned on then
# we will build libsfcCimXmlCodec
#
if !LOCAL_CONNECT_NO_INDICATION
libsfcCimXmlCodec_la_SOURCES = \
   cimXmlOps.y \
   cimXmlParserProcessed.c \
   cimRequest.c \
   $(CIMRS_PROCESSOR)
libsfcCimXmlCodec_la_LIBADD=-lsfcBrokerCore 
libsfcCimXmlCodec_la_DEPENDENCIES=libsfcBrokerCore.la
endif

if SLP
SLP_SRC = profileProvider.c \
          cimslpCMPI.c \
          cimslpSLP.c \
          cimslpUtil.c
SLP_INC = cimslpCMPI.h \
          cimslpSLP.h \
          cimslpUtil.h

else
SLP_SRC =
SLP_INC =
endif

if QUALREP
libsfcQualifierProvider_la_SOURCES = \
   qualifierProvider.c
libsfcQualifierProvider_la_LIBADD=-lsfcBrokerCore -lsfcFileRepository
libsfcQualifierProvider_la_DEPENDENCIES=libsfcBrokerCore.la libsfcFileRepository.la
endif

sfcbd_SOURCES=sfcBroker.c
sfcbd_LDADD=-lsfcBrokerCore $(CIMXMLCODEC_LIBS_LINK) $(HTTP_ADAPTER_LIBS_LINK)
sfcbd_DEPENDENCIES=libsfcBrokerCore.la $(CIMXMLCODEC_LIBS) $(HTTP_ADAPTER_LIBS)

sfcbmofpp_SOURCES=mofpp.c

sfcbdump_SOURCES=sfcbdump.c

sfcbinst2mof_SOURCES=sfcbinst2mof.c
sfcbinst2mof_LDADD = -lsfcFileRepository -lsfcBrokerCore

sfcbtrace_SOURCES=sfcbtrace.c
sfcbtrace_LDADD = -lsfcBrokerCore

classSchema2c_SOURCES=classSchema2c.c
classSchema2c_LDADD=-lsfcBrokerCore
classSchema2c_DEPENDENCIES=libsfcBrokerCore.la

sfcbsem_SOURCES=sfcbsem.c

noinst_HEADERS=array.h $(SLP_INC) httpComm.h control.h    providerMgr.h \
	constClass.h   msgqueue.h     providerRegister.h \
	cimXmlParser.h    native.h       support.h cimXmlGen.h \
	cimRequest.h cimRsRequest.h objectImpl.h   trace.h \
	mlog.h \
	cmpiftx.h cmpimacsx.h \
	fileRepository.h internalProvider.h \
	selectexp.h queryOperation.h \
	sfcVersion.h mrwlock.h avltree.h \
        cimcClientSfcbLocal.h $(QUALREP_HEADER) cmpidtx.h classSchemaMem.h \
        objectpath.h instance.h $(SLP_HEADER) classProviderCommon.h

man_MANS=$(MANFILES)

EXTRA_DIST=sfcb.cfg.pre.in sfcb.spec sfcbrepos.sh.in sfcbstage.sh.in \
	sfcbunstage.sh.in sfcbuuid.sh.in sfcb.init-redhat.in sfcb.init-suse.in \
	sfcb.init-none.in cimXmlParser.c footprint.pl test \
    sblim-sfcb.service.pre.in

dist_sfcbdata_SCRIPTS=genSslCert.sh getSchema.sh test/stageschema.sh

if ACCOUNT_PASSTHRU
ACCOUNT_PASSTHRU_MOF = cimAccountPassthrough.mof
else
ACCOUNT_PASSTHRU_MOF = 
endif

dist_sfcbdata_DATA=default.reg 10_interop.mof 20_indication.mof indication.mof $(ACCOUNT_PASSTHRU_MOF)

nodist_bin_SCRIPTS=sfcbrepos sfcbstage sfcbunstage sfcbuuid

dist_bin_SCRIPTS=wbemcat xmltest

init_SCRIPTS=sfcb

sfcbdoc_DATA=README AUTHORS COPYING

sfcbconf_DATA=sfcb.cfg

if PAM
dist_pam_DATA=pam/sfcb
endif

if DOCS
noinst_DATA=doc/html/index.html

doc/html/index.html: sfcb-dox.conf
	$(DOXYGEN) sfcb-dox.conf
endif

SUFFIXES = .1 .1.pre

.1.pre.1:
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

sfcbrepos: sfcbrepos.sh
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

sfcbstage: sfcbstage.sh
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

sfcbunstage: sfcbunstage.sh
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

sfcbuuid: sfcbuuid.sh
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

sfcb.cfg: sfcb.cfg.pre
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

getSchema.sh: getSchema.sh.pre
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

if SETTABLERETRY
20_indication.mof: 20_indication.mof.pre
	sed -e s?SETTABLE?TRUE?g $< > $@
else
20_indication.mof: 20_indication.mof.pre
	sed -e s?SETTABLE?FALSE?g $< > $@
endif

sfcb: sfcb.$(INIT_STYLE) sblim-sfcb.service
	sed -e s?$$\{prefix\}?$(prefix)?g \
	-e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

sblim-sfcb.service: sblim-sfcb.service.pre
	sed -e s?$$\{exec_prefix\}?$(prefix)?g $< > $@

providerRegister: providerRegister.c
	touch $@

unittest:
	cd test && sh check_all.sh

install-data-local:
	if test -d $(systemddir); then cp $(srcdir)/sblim-sfcb.service $(systemddir); fi; 
	test -d $(DESTDIR)$(sfcbstatedir)/registration/repository || $(mkdir_p) $(DESTDIR)$(sfcbstatedir)/registration/repository
	test -d $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop || $(mkdir_p) $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop
	test -d $(DESTDIR)$(sfcbstatedir)/stage/regs || $(mkdir_p) $(DESTDIR)$(sfcbstatedir)/stage/regs
	$(INSTALL_DATA) ./default.reg $(DESTDIR)$(sfcbstatedir)/stage
	if test -f $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop/interop.mof; then \
	   rm $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop/interop.mof; \
	fi;
	$(INSTALL_DATA) $(srcdir)/10_interop.mof $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop
if INDICATIONS
	$(INSTALL_DATA) 20_indication.mof $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop
	$(INSTALL_DATA) $(srcdir)/indication.mof $(DESTDIR)$(sfcbstatedir)/stage/mofs
endif
if ACCOUNT_PASSTHRU
	$(INSTALL_DATA) $(srcdir)/cimAccountPassthrough.mof $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop
endif
if DOCS
	test -d $(DESTDIR)$(sfcbdocdir)/html || $(mkdir_p) $(DESTDIR)$(sfcbdocdir)/html
	$(INSTALL) -m 644 $(srcdir)/doc/html/* $(DESTDIR)$(sfcbdocdir)/html
endif 

uninstall-local:
	rm -f $(DESTDIR)$(sfcbstatedir)/stage/default.reg
	rm -f $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop/10_interop.mof
	rm -f $(systemddir)/sblim-sfcb.service
if INDICATIONS
	rm -f $(DESTDIR)$(sfcbstatedir)/stage/mofs/root/interop/20_indication.mof
	rm -f $(DESTDIR)$(sfcbstatedir)/stage/mofs/indication.mof
endif
if DOCS
	rm -rf $(DESTDIR)$(sfcbdocdir)/html
endif
if TEST_ENABLED
	./test/stageschema.sh -d test/schema -p $(prefix) -u
endif

clean-local:
	rm -f sfcbrepos sfcbstage sfcbunstage sfcbuuid sfcb.cfg getSchema.sh sfcb \
		sfcb.init-redhat sfcb.init-suse sfcb.init-none sfcb-dox.conf \
                cimXmlParserProcessed.c sblim-sfcb.service 20_indication.mof \
		*.gcno *.gcda *.gcov $(MANFILES) test/TestProviders/tests/RIModIS.XML
	rm -rf doc/* coverage/ 

dist-hook:
	test -d "$(distdir)" &&	rm -rf `find $(distdir) -type d -name CVS`

install-cimschema: getSchema.sh
	test -d $(DESTDIR)$(sfcbdatadir) || $(mkdir_p) $(DESTDIR)$(sfcbdatadir)
	sh getSchema.sh -f $(DESTDIR)$(sfcbdatadir)

create-sslkeys:
if SSL
	$(sfcbdatadir)/genSslCert.sh $(sfcbconfdir)
endif

footprint-sloc:
	./footprint.pl -sq;

footprint-mem:
	./footprint.pl -mq;

footprint:
	@flist='$(sbin_PROGRAMS) $(bin_PROGRAMS) $(sfcblib_LTLIBRARIES) $(dist_sfcbdata_SCRIPTS) $(init_SCRIPTS) $(man1_MANS) $(dist_man1_MANS) $(dist_pam_DATA) $(dist_sfcbdata_DATA) $(sfcbconf_DATA) $(sfcbdoc_DATA) $(sfcb_sharedobjects) '; \
	rm -f "MANIFEST"; \
	for p in $$flist; do \
		f=`echo $$p|sed 's/$(EXEEXT)$$//'`; \
		echo "$$f" >> MANIFEST; \
	done;\
	./footprint.pl;

if TEST_ENABLED
testprep:
if GCOV
	mkdir -p ./coverage
	lcov --zerocounters --directory .libs/
endif

testreport:
if GCOV
    # Ignore errors from lcov since it always seems to have a problem
	- lcov --capture --directory .libs --output-file ./coverage/lcovtest.info --ignore-errors gcov,source
	lcov --remove ./coverage/lcovtest.info "/usr*" --output-file ./coverage/cov_stripped.info
	genhtml --output-directory ./coverage ./coverage/cov_stripped.info
	@echo "****** Coverage report generated in ./coverage/index.html"
endif

test: testprep check testreport
endif

pretty:
	for i in `find $(srcdir) -name \*.[ch]`; do \
          sed -i '/\/\* MODELINES \*\//,$$ d' $$i ; \
          indent $$i ; \
	  cat extra/modelines >> $$i ; \
	done;

postinstall: install-cimschema create-sslkeys
	test -f $(DESTDIR)$(sfcbstatedir)/registration/providerRegister || $(INSTALL_DATA) $(DESTDIR)$(sfcbstatedir)/stage/default.reg $(DESTDIR)$(sfcbstatedir)/registration/providerRegister 
if TEST_ENABLED
	$(srcdir)/test/stageschema.sh -d $(srcdir)/test/schema -p $(prefix)
endif
