	echo "Checking for PCRE ..."

	PCREINC=""
	PCRELIB=""
	for DIR in /opt/pcre* /usr/local/pcre* /usr/local /usr /usr/pkg /opt/csw
	do
		if test -f $DIR/include/pcre.h
		then
			PCREINC=$DIR/include
		fi
		if test -f $DIR/include/pcre/pcre.h
		then
			PCREINC=$DIR/include/pcre
		fi

		if test -f $DIR/lib/libpcre.so
		then
			PCRELIB=$DIR/lib
		fi
		if test -f $DIR/lib/libpcre.a
		then
			PCRELIB=$DIR/lib
		fi
	done

	if test -z "$PCREINC" -o -z "$PCRELIB"; then
		echo "PCRE include- or library-files not found. These are REQUIRED for hobbitd"
		echo "PCRE can be found at http://www.pcre.org/"
		exit 1
	else
		cd build
		OS=`uname -s` $MAKE -f Makefile.test-pcre clean
		OS=`uname -s` PCREINC="-I$PCREINC" $MAKE -f Makefile.test-pcre test-compile
		if [ $? -eq 0 ]; then
			echo "Found PCRE include files in $PCREINC"
		else
			echo "ERROR: PCRE include files found in $PCREINC, but compile fails."
			exit 1
		fi

		OS=`uname -s` PCRELIB="-L$PCRELIB" $MAKE -f Makefile.test-pcre test-link
		if [ $? -eq 0 ]; then
			echo "Found PCRE libraries in $PCRELIB"
		else
			echo "ERROR: PCRE library files found in $PCRELIB, but link fails."
			exit 1
		fi
		OS=`uname -s` $MAKE -f Makefile.test-pcre clean
		cd ..
	fi


