# $Id: configure.ac,v 1.106 2010/02/18 18:53:28 buccella Exp $
# 
# configure.ac
#
# © Copyright IBM Corp. 2005, 2007
#
# THIS FILE IS PROVIDED UNDER THE TERMS OF THE ECLIPSE PUBLIC LICENSE
# ("AGREEMENT"). ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS FILE
# CONSTITUTES RECIPIENTS ACCEPTANCE OF THE AGREEMENT.
#
# You can obtain a current copy of the Eclipse Public License from
# http://www.opensource.org/licenses/eclipse-1.0.php
#
# Author:        Viktor Mihajlovski <mihajlov@de.ibm.com>
# Contributions: Adrian Schuur <schuur@de.ibm.com>
#                Sven Schuetz <sven@de.ibm.com>
#
# Description:
#
# Configure process input for sfcb.
# 

# Process this file with autoconf to produce a configure script.

AC_INIT(Small Footprint CIM Broker, 1.4.2, sblim-devel@lists.sourceforge.net, sblim-sfcb)
AC_CONFIG_SRCDIR([providerDrv.c])

#disable "seems to ignore the --datarootdir setting" warnings
#AC_DEFUN([AC_DATAROOTDIR_CHECKED])

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER([config.h])

#Source files will use the local sfcCommon files if this is set.
#Reverse this to use the shared sfcCommon library.
AC_DEFINE(SFCCOMMON,1,[Local sfcCommon files used.])

AC_ARG_ENABLE(debug,
	[AC_HELP_STRING([--disable-debug],
		[disable debug mode for SFCB.])],
	[enable_debug=$enableval],
	[enable_debug="yes"]
	)	

AC_ARG_ENABLE(ssl,
	[AC_HELP_STRING([--enable-ssl],
		[include ssl support for SFCB.])])

AC_ARG_ENABLE(slp,
	[AC_HELP_STRING([--enable-slp],
		[include slp agent for SFCB.])])
		
AC_ARG_ENABLE(slp-hostname-lib,
   [AC_HELP_STRING([--enable-slp-hostname-lib],
      [aquire hostname for slp registration from external library.])])

AC_ARG_ENABLE(qualifierrep,
	[AC_HELP_STRING([--disable-qualifierrep],
		[disable qualifier repository support.])],
	[enable_qualifierrep=$enableval],
	[enable_qualifierrep="yes"]
	)		

AC_ARG_ENABLE(indications,
	[AC_HELP_STRING([--disable-indications],
		[disable indication support for SFCB])],
	[enable_indications=$enableval],
	[enable_indications="yes"]
	)

AC_ARG_ENABLE(local_connect_only,
	[AC_HELP_STRING([--enable-local-connect-only],
		[SFCB will only support local connection. Http server will not be built.])],
	[enable_local_connect_only=$enableval],
	[enable_local_connect_only="no"]
	)

AC_ARG_ENABLE(default-properties,
	[AC_HELP_STRING([--disable-default-properties],
		[disable generation of default instance properties for SFCB.])],
	[enable_default_properties=$enableval],
	[enable_default_properties="yes"]
	)

AC_ARG_ENABLE(optimized-enumeration,
	[AC_HELP_STRING([--enable-optimized-enumeration],
		[Enable enumeration call optimization for polymorphic invocations.])],
	)

AC_ARG_ENABLE(pam,
	[AC_HELP_STRING([--enable-pam],
		[Enable PAM Basic authentication support for SFCB.])])

AC_ARG_ENABLE(docs,
	[AC_HELP_STRING([--enable-docs],
		[Enable generation of API documentation.])])

AC_ARG_ENABLE(ipv6,
	[AC_HELP_STRING([--enable-ipv6],
		[include ipv6 support for SFCB.])])

AC_ARG_ENABLE(tests,
	[AC_HELP_STRING([--enable-tests(=TESTS)],
		[Enable listed tests. If none are listed, all are enabled.])])

AC_ARG_ENABLE(settableretry,
	[AC_HELP_STRING([--enable-settableretry],
		[Allow reliable indication params to be settable.])])

AC_ARG_ENABLE(uds,
	[AC_HELP_STRING([--enable-uds],
		[Enable UDS authentication.])])

AC_ARG_ENABLE(request-types,
	[AC_HELP_STRING([--enable-request-types(=TYPES)],
		[Enable support of listed request types. If none are listed, all are enabled.])])

AC_ARG_ENABLE(expired-pw-update,
	[AC_HELP_STRING([--enable-expired-pw-update],
		[allow a user with an expired account to invoke a password update for CIM_Account (see docs).])])

# Size checks
AC_CHECK_SIZEOF(void*)
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(short)

if test "$ac_cv_sizeof_voidp" = 4
then
  if test "$ac_cv_sizeof_short" = 2
  then
     AC_DEFINE(SFCB_HALFWORD,short,[integer od half pointer size])
  else
   AC_ERROR([Unsupported 32-bit platform: short size is $ac_cv_sizeof_short, expected 2.])
  fi
elif  test "$ac_cv_sizeof_voidp" = 8
then
  if test "$ac_cv_sizeof_int" = 4
  then
     AC_DEFINE(SFCB_HALFWORD,int,[integer od half pointer size])
  else
   AC_ERROR([Unsupported 64-bit platform: int size is $ac_cv_sizeof_int, expected 4])
  fi
else
   AC_ERROR([Unsupported platform, invalid pointer size $ac_cv_sizeof_voidp.])
fi 

# Configuration Arguments
AC_ARG_VAR([CIMSCHEMA_SOURCE],[The URL or path name of the CIM Schema ZIP File.])
AC_ARG_VAR([CIMSCHEMA_MOF],[The file name of the top level MOF (CIMSchema.mof)])
AC_ARG_VAR([CIMSCHEMA_SUBDIRS],[Set this to any value if the unpacked CIM Schema directory has subdirectories])
AC_SUBST(CIMSCHEMA_SOURCE)
AC_SUBST(CIMSCHEMA_MOF)
AC_SUBST(CIMSCHEMA_SUBDIRS)


AC_DISABLE_STATIC

if test "$enable_debug" != "yes" ; then
   CFLAGS="`echo $CFLAGS | sed -e 's/-g//'`"
   CXXFLAGS="`echo $CXXFLAGS | sed -e 's/-g//'`"
fi

if test "$enable_debug" == "yes"; then
   SFCB_CPPFLAGS="-D SFCB_DEBUG $SFCB_CPPFLAGS"
fi

if test "$enable_default_properties" == "yes"; then
   AC_DEFINE(HAVE_DEFAULT_PROPERTIES,1,[Default instance property generation enabled.])
fi

if test "$enable_slp" == "standalone"; then
   # AC_DEFINE(HAVE_SLP_ALONE,,[SLP standalone support enabled.])
   AC_MSG_ERROR(standalone slp agent is no longer supported.)
fi

if test "$enable_slp" == "yes"; then
   AC_DEFINE(HAVE_SLP,,[SLP support enabled.])
   LOAD_PROFILE_PROVIDER=
else
   LOAD_PROFILE_PROVIDER='#'
fi
AC_SUBST(LOAD_PROFILE_PROVIDER)

if test "$enable_slp_hostname_lib" == "yes"; then
   AC_DEFINE(SLP_HOSTNAME_LIB,,[SLP Hostname lib enabled])
fi

if test "$enable_expired_pw_update" == "yes"; then
   LOAD_SFCBACCOUNT_PROVIDER=
   AC_DEFINE(ALLOW_UPDATE_EXPIRED_PW,1,[Expired Account Password Update enabled])
else
   LOAD_SFCBACCOUNT_PROVIDER='#'
fi
AC_SUBST(LOAD_SFCBACCOUNT_PROVIDER)

# Check and configure requested tests.
if test "$enable_tests" == "yes"; then
if test "$enable_local_connect_only" == "yes"; then
    enable_tests="localtests,gcov"
else
    # This means all tests should be on
    enable_tests="unit,gcov,wbemcli,xml,commands,slp,providers,cimrs"
fi
fi
if test "$enable_tests" != ""; then
    enable_settableretry="yes"
fi


OLDIFS="$IFS"
IFS=","
for testcase in $enable_tests;
do
    case $testcase in
        gcov)
            test_gcov=yes;;
        wbemcli)
            test_wbemcli=yes;;
        pywbem)
            test_pywbem=yes;;
        unit|unittest)
            test_unit=yes;;
        xml|xmltest)
            test_xml=yes;;
        cimrs)
            test_cimrs=yes;;
        commands|command|cmd|cmds)
            test_cmd=yes;;
	slp)
	    if test "$enable_slp" == "yes"; then
	       test_slp=yes
	    fi
	    ;;
        providers)
            test_providers=yes;;
        localtests)
	    test_local=yes;;
        *)
            echo "Unknown testcase $testcase";;
    esac
        
done
IFS="$OLDIFS"
AM_CONDITIONAL(TEST_ENABLED,[test "$enable_tests"])
AM_CONDITIONAL(UNITTEST,[test "$test_unit" == "yes"])
AM_CONDITIONAL(GCOV,[test "$test_gcov" == "yes"])
AM_CONDITIONAL(WCTEST,[test "$test_wbemcli" == "yes"])
AM_CONDITIONAL(XMLTEST,[test "$test_xml" == "yes"])
AM_CONDITIONAL(CIMRSTEST,[test "$test_cimrs" == "yes"])
AM_CONDITIONAL(PYWBEMTEST,[test "$test_pywbem" == "yes"])
AM_CONDITIONAL(UDSTEST,[test "$enable_uds" == "yes"])
AM_CONDITIONAL(CMDTEST,[test "$test_cmd" == "yes"])
AM_CONDITIONAL(SLPTEST,[test "$test_slp" == "yes"])
AM_CONDITIONAL(PROVTEST,[test "$test_providers" == "yes"])
AM_CONDITIONAL(PROVTEST,[test "$test_local" == "yes"])
if [test "$enable_tests"]; then
    # The order of TEST_SUBDIRS determines test
    # execution order.
    TEST_SUBDIRS="."
    AC_CONFIG_FILES([test/finaltest/Makefile])
    # Enable the various tests
    if [test "$test_providers" == "yes"]; then
       TEST_SUBDIRS="$TEST_SUBDIRS TestProviders/tests"
       AC_CONFIG_FILES([test/TestProviders/tests/Makefile])
    fi

    if [test "$test_cmd" == "yes"]; then
        TEST_SUBDIRS="$TEST_SUBDIRS commands"
        AC_CONFIG_FILES([test/commands/Makefile])
    fi

    if [test "$test_wbemcli" == "yes"]; then
        TEST_SUBDIRS="$TEST_SUBDIRS wbemcli"
        AC_CONFIG_FILES([test/wbemcli/Makefile])
    fi

    if [test "$test_pywbem" == "yes"]; then
        TEST_SUBDIRS="$TEST_SUBDIRS pywbem"
        AC_CONFIG_FILES([test/pywbem/Makefile])
    fi

    if [test "$test_xml" == "yes"]; then
        TEST_SUBDIRS="$TEST_SUBDIRS xmltest"
        AC_CONFIG_FILES([test/xmltest/Makefile])
    fi

    if [test "$test_cimrs" == "yes"]; then
        TEST_SUBDIRS="$TEST_SUBDIRS cimrstest"
        AC_CONFIG_FILES([test/cimrstest/Makefile])
    fi

    if [test "$test_unit" == "yes"]; then
        TEST_SUBDIRS="$TEST_SUBDIRS unittest"
        AC_CONFIG_FILES([test/unittest/Makefile])
        AC_DEFINE(UNITTEST,,[Unittest enabled])
    fi

    if [test "$test_slp" == "yes"];then
	TEST_SUBDIRS="$TEST_SUBDIRS slptest"
	AC_CONFIG_FILES([test/slptest/Makefile])
    fi

    if [test "$test_local" == "yes"];then
       TEST_SUBDIRS="$TEST_SUBDIRS localtests"
       AC_CONFIG_FILES([test/localtests/Makefile])	
    fi

    TEST_SUBDIRS="$TEST_SUBDIRS finaltest"
    AC_SUBST(TEST_SUBDIRS)
fi

if [test "$test_gcov" == "yes"]; then
    AC_CHECK_LIB(gcc,main)
    AC_CHECK_LIB(gcov,main)
    AC_PATH_PROG(LCOV,lcov,yes,no)
    AC_PATH_PROG(GENHTML,genhtml,yes,no)
    if test "$LCOV" == "no" -o "$GENHTML" == "no" ; then
       AC_MSG_ERROR([lcov and genhtml are required for profiling.])
    fi
    SFCB_CPPFLAGS="-D SFCB_GCOV $SFCB_CPPFLAGS"
fi
# End of test configuration 

# Check and configure requested handlers.
if test "$enable_request_types" == "yes"; then
    enable_request_types="cimxml,cimrs"
fi
if test "$enable_request_types" == ""; then
    enable_request_types="cimxml,cimrs"
fi
OLDIFS="$IFS"
IFS=","
for rt in $enable_request_types;
do
    case $rt in
        cimxml|CIMxml|CIMXML|cimXML)
            handler_CIMXML=yes;;
        cimrs|CIMrs|CIMRS|cimRS)
            handler_CIMRS=yes;;
        *)
            handler_ERROR=yes
            echo "Unknown request type $rt";;
    esac
done
IFS="$OLDIFS"
if test "$handler_ERROR" == "yes"; then
   AC_MSG_ERROR([Unknown request type.])
fi
if test "$handler_CIMXML" == "yes"; then
   AC_DEFINE(HANDLER_CIMXML,,["CIMxml handler enabled"])
fi
if test "$handler_CIMRS" == "yes"; then
   AM_CONDITIONAL(CIMRS,[test "$handler_CIMRS" == "yes"])
   AC_DEFINE(HANDLER_CIMRS,,["CIMrs handler enabled"])
fi
# End of parser config

if test "$enable_uds" == "yes"; then
   AC_DEFINE(HAVE_UDS,,[UDS authentication enabled])
fi

if test "$enable_settableretry" == "yes"; then
   AC_DEFINE(SETTABLERETRY,,[Settable indication parameters enabled])
fi

if test "$enable_qualifierrep" == "yes"; then
   LOAD_QUALIFIER_PROVIDER=
   AC_DEFINE(HAVE_QUALREP,,[Qualifier repository support enabled.])
else
   LOAD_QUALIFIER_PROVIDER='#'
fi
AC_SUBST(LOAD_QUALIFIER_PROVIDER)
AC_SUBST(SFCB_LIBPAM)

if test "$enable_optimized_enumeration" == "yes"; then
   AC_DEFINE(HAVE_OPTIMIZED_ENUMERATION,1,[Enumeration optimization enabled.])
fi

if test "$enable_pam" == "yes"; then
   AC_DEFINE(HAVE_PAM,,[PAM support enabled.])
   AC_CHECK_LIB(pam,main,[SFCB_LIBPAM=-lpam],[AC_MSG_ERROR(Could not find required pam library.)])
   SFCB_CONF_BASICAUTHLIB=sfcBasicPAMAuthentication   
   SFCB_CONF_DOBASICAUTH=true
else
   SFCB_CONF_BASICAUTHLIB=sfcBasicAuthentication   
   SFCB_CONF_DOBASICAUTH=false
fi
AC_SUBST(SFCB_CONF_DOBASICAUTH)
AC_SUBST(SFCB_CONF_BASICAUTHLIB)

if test "$enable_ipv6" == "yes"; then
   AC_DEFINE(HAVE_IPV6,,[IPv6 support enabled.])
fi

AC_CANONICAL_HOST
case $host_os in
     openedition*)	SFCB_CMPI_PLATFORM="-D CMPI_PLATFORM_LINUX_GENERIC_GNU"
		SFCB_CMPI_OS="-D CMPI_OS_LINUX"
		SFCB_CPPFLAGS=-W"c,SSCOM"
		SFCB_CPPFLAGS="-D_OPEN_SOURCE=3 -D_ENHANCED_ASCII_EXT=0xFFFFFFFF $SFCB_CPPFLAGS"
		SFCB_CFLAGS=-W"c,ASCII,dll,expo,langlvl(extended),float"
		SFCB_LDFLAGS=-W"l,XPLINK,dll,EDIT=NO"
		CXX="c++ -+"
		;;
     linux*)	SFCB_CMPI_PLATFORM="-D CMPI_PLATFORM_LINUX_GENERIC_GNU"
		SFCB_CMPI_OS="-D CMPI_OS_LINUX"
                SFCB_CFLAGS="-Wall -fno-strict-aliasing -fsigned-char $SFCB_CFLAGS"
		;;
     hpux*)	SFCB_CMPI_PLATFORM="-D CMPI_OS_HPUX"
		case $host_arch in
		     parisc*) SFCB_CMPI_PLATFORM="-D CMPI_PLATFORM_HPUX_PARISC_ACC"
			      ;;
		esac
		;;
esac


case $host_cpu in
     i*86)     AC_DEFINE(SFCB_IX86,1,[Intel i386 CPU])
	       ;;     
     powerpc*)  CFLAGS="$CFLAGS -O0"
	       ;;     
esac

AC_SUBST(SFCB_CMPI_PLATFORM)
AC_SUBST(SFCB_CMPI_OS)
AC_SUBST(SFCB_LIBZ)
AC_SUBST(SFCB_CFLAGS)
AC_SUBST(SFCB_CPPFLAGS)
AC_SUBST(SFCB_LDFLAGS)

# Checks for programs.
AC_PROG_CC
AC_PROG_LEX
case $LEX in
 :) AC_MSG_ERROR([No lex program found]);;
esac 
AC_CHECK_PROG(YACC,bison,[bison -y])
if test -z "${YACC}"
then
   AC_MSG_WARN([GNU bison is required (only for rebuilding the sfcb parser code).])
fi

AC_PROG_LIBTOOL
AC_CHECK_PROG(HAVE_UNZIP,unzip,"yes","no",)
if test "$HAVE_UNZIP" = "no" ; then
  AC_MSG_ERROR([Could not find unzip])
fi

# Checks for libraries.
AC_CHECK_LIB(pthread,main)
AC_CHECK_LIB(dl,main)
AC_CHECK_LIB(z,main,[SFCB_LIBZ=-lz],[AC_MSG_ERROR([Could not find required libz])])
AC_CHECK_LIB(sfcUtil,newStringBuffer,, \
  [AC_MSG_ERROR([Could not find required sfcUtil. Is sfcCommon installed?])])
if test "$enable_indications" = "yes" ; then
   LOAD_INDICATION_PROVIDER=
   AC_DEFINE(HAVE_INDICATIONS,1,[Indication support enabled.])
   AC_CHECK_LIB(curl,main)
else
   LOAD_INDICATION_PROVIDER='#'
fi

AC_SUBST(LOAD_INDICATION_PROVIDER)

if test "$enable_ssl" = "yes"; then
   AC_CHECK_LIB(ssl,main)
   SFCB_CONF_HTTPS=true
   SFCB_CONF_HTTP=false
else
   SFCB_CONF_HTTPS=false	   
   SFCB_CONF_HTTP=true
fi


#
# if enable_local_connect_only is on then ignore whatever is set by enable_ssl
#
if test "$enable_local_connect_only" = "yes" ; then
   AC_DEFINE(LOCAL_CONNECT_ONLY_ENABLE,1,[Local Connect only.])
   SFCB_CONF_HTTPS=false	   
   SFCB_CONF_HTTP=false
fi

AC_SUBST(SFCB_CONF_HTTPS)
AC_SUBST(SFCB_CONF_HTTP)

if test "$enable_slp"; then
	#defined in acinclude.m4
	AC_CHECK_HEADERS([slp.h],[],[AC_MSG_ERROR(no. Sorry cannot find slp.h.)])
	AC_CHECK_LIB(slp,SLPReg,[],[AC_MSG_ERROR(no. Sorry cannot find libslp.)])
fi

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h limits.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h zlib.h])
AC_CHECK_HEADERS([cmpi/cmpimacs.h cmpi/cmpift.h cmpi/cmpidt.h],[],[AC_MSG_ERROR([Could not find required CPMI header.])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_BIGENDIAN
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MKTIME
AC_TYPE_SIGNAL
AC_FUNC_STRFTIME
AC_FUNC_WAIT3
AC_CHECK_FUNCS([alarm bzero gettimeofday localtime_r memmove memset socket strcasecmp strchr strdup strncasecmp strpbrk strrchr strspn strstr tzset])

# Check for Linux System Style

if test -f /etc/redhat-release 
then
   INIT_STYLE=init-redhat
elif test -f /etc/SuSE-release
then 
   INIT_STYLE=init-suse
else
   INIT_STYLE=init-none
fi
AC_SUBST(INIT_STYLE)

# Check for doxygen for generating documentation
if test "$enable_docs" = "yes"; then
   AC_CHECK_PROG(DOXYGEN,doxygen,[doxygen])
   if test -z "${DOXYGEN}"
   then
      AC_MSG_ERROR([doxygen is required to build API documentation.])
   fi
fi

if test -d $srcdir/mofc; then
   MOFC_DIR=mofc
   export sfcb_recursed=yes
   export with_sfcb=yes
   export enable_namespace_kludge=yes
   AC_CONFIG_SUBDIRS([mofc])
   AC_SUBST(MOFC_DIR)
fi

if test -d $srcdir/cmpi-devel; then
   CMPIDEV_DIR=cmpi-devel
   AC_CONFIG_SUBDIRS([cmpi-devel])
   AC_SUBST(CMPIDEV_DIR)
fi

AM_CONDITIONAL(INDICATIONS,[test "$enable_indications" == "yes"]) 
AM_CONDITIONAL(SETTABLERETRY,[test "$enable_settableretry" == "yes"]) 
AM_CONDITIONAL(SSL,[test "$enable_ssl" == "yes"]) 
AM_CONDITIONAL(SLP,[test "$enable_slp" == "yes"]) 
AM_CONDITIONAL(SLP_HOSTNAME_LIB,[test "$enable_slp_hostname_lib" == "yes"])
AM_CONDITIONAL(SLP_ALONE,[test "$enable_slp" == "standalone"]) 
AM_CONDITIONAL(QUALREP,[test "$enable_qualifierrep" == "yes"]) 
AM_CONDITIONAL(PAM,[test "$enable_pam" == "yes"]) 
AM_CONDITIONAL(DOCS,[test "$enable_docs" == "yes"])
AM_CONDITIONAL(IPV6,[test "$enable_ipv6" == "yes"])
AM_CONDITIONAL(LOCAL_CONNECT_NO_INDICATION,[test "$enable_local_connect_only" == "yes" -a "$enable_indications" == "no"])
AM_CONDITIONAL(LOCAL_CONNECT_ONLY,[test "$enable_local_connect_only" == "yes"])
AM_CONDITIONAL(ACCOUNT_PASSTHRU,[test "$enable_expired_pw_update" == "yes"])

AC_CONFIG_FILES([Makefile sfcb.spec sfcbrepos.sh sfcbstage.sh sfcbunstage.sh 
	         sfcbuuid.sh sfcb.cfg.pre getSchema.sh.pre 20_indication.mof.pre
		 sfcb.init-redhat sfcb.init-suse sfcb.init-none default.reg sblim-sfcb.service.pre
		 man/genSslCert.1.pre  man/sfcbmof.1.pre 
                 man/sfcbunstage.1.pre man/sfcbuuid.1.pre man/getSchema.1.pre
		 man/sfcbrepos.1.pre  man/wbemcat.1.pre
		 man/sfcbd.1.pre man/sfcbstage.1.pre man/xmltest.1.pre 
		 sfcb-dox.conf test/Makefile])
AC_OUTPUT

echo =================================================================
echo Configuration Features:
echo -e "ssl"\\t\\t\\t\\t"${enable_ssl:-no}"
echo -e "slp"\\t\\t\\t\\t"${enable_slp:-no}"
echo -e "slp hostname lib"\\t\\t"${enable_slp_hostname_lib:-no}"
echo -e "IPv6"\\t\\t\\t\\t"${enable_ipv6:-no}"
echo -e "indications"\\t\\t\\t"${enable_indications}"
echo -e "qualifierrep"\\t\\t\\t"${enable_qualifierrep:-no}"
echo -e "Documentation"\\t\\t\\t"${enable_docs:-no}"
echo -e "PAM authentication"\\t\\t"${enable_pam:-no}"
echo -e "local connect only "\\t\\t"${enable_local_connect_only}"
echo -e "default instance properties"\\t"${enable_default_properties}"
echo -e "uds"\\t\\t\\t\\t"${enable_uds:-no}"
echo -e "tests"\\t\\t\\t\\t"${enable_tests:-no}"
echo -e "debug"\\t\\t\\t\\t"${enable_debug:-no}"
echo -e "Request types"\\t\\t\\t"${enable_request_types}"
echo -e "Allow expired account pw update"\\t"${enable_expired_pw_update:-no}"
echo -e "Settable retry parameters"\\t"${enable_settableretry:-no}"
echo =================================================================
echo
echo "sfcb configuration complete.  you may now run 'make'"
echo
