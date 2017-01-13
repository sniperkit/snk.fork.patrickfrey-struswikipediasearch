#!/bin/sh

tarfile=$1
prefix=$2
outputfile=$3
srcprefix=$4

if [ x$prefix = 'x' ]
then
	prefix=tmp
fi
if [ x$outputfile = 'x' ]
then
	outputfile=docs.dump.$jobid.txt
fi

mkdir -p $prefix$jobid
tar -C $prefix$jobid/ -xvzf $1
for ff in `find $prefix$jobid/ -name "*.xml" | sort`
do
echo "analyze $ff"
strusAnalyze -M /usr/local/lib/strus/modules -m analyzer_wikipedia_search -R "$srcprefix"resources/ -D "orig,sent=' .\n',para=' .\n',start=' .\n'" "$srcprefix"config/wikipedia.ana $ff | iconv -c -f utf-8 -t utf-8 - | "$srcprefix"scripts/nlpclean.sh | perl -C -pe 's/[~_=\/\#-\+\-]/ /g' | perl -C -pe 's/[\<\>()\|\[\]\{\}]/ \,/g' | perl -C -pe 's/[,]+\./\./g' | perl -C -pe 's/([\.])(^[0-9])/ \1 \2/g' | perl -C -pe 's/([\;\,\!\?\:])/ \1 /g' | perl -C -pe 's/([\\])/ /g' >> $outputfile
done

rm -Rf $prefix$jobid/

