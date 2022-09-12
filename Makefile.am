AUTOMAKE_OPTIONS = foreign dist-lzip no-dist-gzip subdir-objects
ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libdsss-transfer.la
libdsss_transfer_la_SOURCES = src/dsssframe.h src/dsssframegen.c src/dsssframesync.c src/dsss-transfer.c
libdsss_transfer_la_LDFLAGS = -version-info 1:0:0

bin_PROGRAMS = dsss-transfer
dsss_transfer_SOURCES = src/main.c
dsss_transfer_LDADD = libdsss-transfer.la

include_HEADERS = src/dsss-transfer.h

examplesdir =
examples_PROGRAMS = examples/duplex examples/echo-server
examples_duplex_SOURCES = examples/duplex.c
examples_duplex_CFLAGS = -I src
examples_duplex_LDADD = libdsss-transfer.la -lpthread
examples_echo_server_SOURCES = examples/echo-server.c
examples_echo_server_CFLAGS = -I src
examples_echo_server_LDADD = libdsss-transfer.la

check_PROGRAMS = tests/test-library-callback tests/test-library-file
tests_test_library_callback_SOURCES = tests/test-library-callback.c
tests_test_library_callback_CFLAGS = -I src
tests_test_library_callback_LDADD = libdsss-transfer.la
tests_test_library_file_SOURCES = tests/test-library-file.c
tests_test_library_file_CFLAGS = -I src
tests_test_library_file_LDADD = libdsss-transfer.la
TESTS = tests/test-library-callback tests/test-library-file tests/test-program.sh

dist_doc_DATA = LICENSE README
EXTRA_DIST = examples/duplex-ppp.sh tests/test-program.sh
