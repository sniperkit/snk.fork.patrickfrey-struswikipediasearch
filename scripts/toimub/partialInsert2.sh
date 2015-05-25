#!/bin/sh

bzip2 -d -c /data/*.xml.bz2 | strusWikimediaToXml -s -r30G,100G - | strusInsert -c500 -f1 -n -m analyzer_wikipedia_search -R /home/pfrey/github/strusWikipediaSearch/resources/ -s "path=/data/storage2; max_open_files=256; write_buffer_size=2M" /home/pfrey/github/strusWikipediaSearch/config/wikipedia.ana -
echo "30G,..rest" inserted into storage 2
