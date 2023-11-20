LIBGSL = $(shell gsl-config --libs)

CFLAGS = -static -O3 -Wall
LDLIBS = -lpthread -lnuma ${LIBGSL}

.PHONY: all clean

all: icollector #tags

icollector: dodump.c dodump.h icollector.c icollector.h

#tags: carrefour.c carrefour.h
#	ctags --totals `find . -name '*.[ch]'`
#	cscope -b -q -k -R -s.

clean_tags:
	#rm cscope.*
	#rm tags

clean: clean_tags
	rm -f icollector *o
