/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "textwolf/istreamiterator.hpp"
#include "textwolf/xmlscanner.hpp"
#include "textwolf/charset.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/thread.hpp"
#include "strus/base/numstring.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/atomic.hpp"
#include "strus/base/local_ptr.hpp"
#include "linkMap.hpp"
#include "documentStructure.hpp"
#include "outputString.hpp"
#include "wikimediaLexer.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <string>
#include <stdexcept>
#include <memory>
#include <vector>
#include <set>
#include <queue>
#include <limits>

static int g_verbosity = 0;
static bool g_beautified = false;
static bool g_dumps = false;
static int g_breakpoint = -1;
static std::string g_outputdir;
static std::string g_origOutputPattern;
static const strus::LinkMap* g_linkmap = NULL;

typedef textwolf::XMLScanner<textwolf::IStreamIterator,textwolf::charset::UTF8,textwolf::charset::UTF8,std::string> XmlScanner;

static std::string attributesToString( const strus::WikimediaLexem::AttributeMap& attributes)
{
	std::ostringstream out;
	strus::WikimediaLexem::AttributeMap::const_iterator ai = attributes.begin(), ae = attributes.end();
	for (int aidx=0; ai != ae; ++ai,++aidx)
	{
		if (aidx) out << ", ";
		out << ai->first << "='" << ai->second << "'";
	}
	return out.str();
}

static void parseDocumentText( strus::DocumentStructure& doc, const char* src, std::size_t size)
{
	strus::WikimediaLexer lexer(src,size);
	int lexemidx = 0;
	bool verboseOutput = (g_verbosity >= 2);

	for (strus::WikimediaLexem lexem = lexer.next(); lexem.id != strus::WikimediaLexem::EoF; lexem = lexer.next(),++lexemidx)
	{
		if (verboseOutput)
		{
			std::cout << "STATE " << doc.statestring() << std::endl;
			std::cout << lexemidx << " LEXEM " << strus::WikimediaLexem::idName( lexem.id) << " " << strus::outputLineString( lexem.value.c_str(), lexem.value.c_str() + lexem.value.size());
			if (!lexem.attributes.empty()) std::cout << " -- " << attributesToString( lexem.attributes);
			std::cout << std::endl;
			if (lexemidx == g_breakpoint)
			{
				std::cout << "... verbose output stops here" << std::endl;
				verboseOutput = false;
			}
		}
		switch (lexem.id)
		{			
			case strus::WikimediaLexem::EoF:
				break;
			case strus::WikimediaLexem::Error:
				doc.addError( std::string("syntax error in document: ") + strus::outputLineString( lexem.value.c_str()));
				break;
			case strus::WikimediaLexem::Text:
				doc.addText( lexem.value);
				break;
			case strus::WikimediaLexem::String:
				doc.closeOpenQuoteItems();
				doc.addQuotationMarker();
				doc.addText( lexem.value);
				doc.addQuotationMarker();
				break;
			case strus::WikimediaLexem::Char:
				doc.addChar( lexem.value);
				break;
			case strus::WikimediaLexem::Math:
				doc.addMath( lexem.value);
				break;
			case strus::WikimediaLexem::BibRef:
				doc.addBibRef( lexem.value);
				break;
			case strus::WikimediaLexem::NoWiki:
				doc.addNoWiki( lexem.value);
				break;
			case strus::WikimediaLexem::NoData:
				doc.addError( std::string("lexem can not be treated as data: ") + strus::outputLineString( lexem.value.c_str()));
				break;
			case strus::WikimediaLexem::Code:
				doc.addCode( lexem.value);
				break;
			case strus::WikimediaLexem::Timestamp:
				doc.addTimestamp( lexem.value);
				break;
			case strus::WikimediaLexem::Url:
				doc.openWebLink( lexem.value);
				doc.closeWebLink();
				break;
			case strus::WikimediaLexem::Redirect:
				doc.addError( "unexpected redirect in document");
				break;
			case strus::WikimediaLexem::OpenHeading:
				doc.openHeading( (int)lexem.idx);
				break;
			case strus::WikimediaLexem::CloseHeading:
				doc.closeHeading();
				break;
			case strus::WikimediaLexem::OpenRef:
				doc.openRef();
				break;
			case strus::WikimediaLexem::CloseRef:
				doc.closeRef();
				break;
			case strus::WikimediaLexem::HeadingItem:
				doc.addHeadingItem();
				break;
			case strus::WikimediaLexem::ListItem:
				doc.openListItem( (int)lexem.idx);
				break;
			case strus::WikimediaLexem::EndOfLine:
				doc.closeOpenEolnItem();
				doc.addText( "\n");
				break;
			case strus::WikimediaLexem::EntityMarker:
				doc.addEntityMarker();
				break;
			case strus::WikimediaLexem::QuotationMarker:
				doc.addQuotationMarker();
				break;
			case strus::WikimediaLexem::DoubleQuoteMarker:
				doc.addDoubleQuoteMarker();
				break;
			case strus::WikimediaLexem::OpenDoubleQuote:
				doc.addDoubleQuoteMarker();
				break;
			case strus::WikimediaLexem::CloseDoubleQuote:
				doc.addDoubleQuoteMarker();
				break;
			case strus::WikimediaLexem::OpenSpan:
				doc.openSpan();
				break;
			case strus::WikimediaLexem::CloseSpan:
				doc.closeSpan();
				break;
			case strus::WikimediaLexem::OpenFormat:
				doc.openFormat();
				break;
			case strus::WikimediaLexem::CloseFormat:
				doc.closeFormat();
				break;
			case strus::WikimediaLexem::OpenBlockQuote:
				doc.openBlockQuote();
				break;
			case strus::WikimediaLexem::CloseBlockQuote:
				doc.closeBlockQuote();
				break;
			case strus::WikimediaLexem::OpenDiv:
				doc.openDiv();
				break;
			case strus::WikimediaLexem::CloseDiv:
				doc.closeDiv();
				break;
			case strus::WikimediaLexem::OpenPoem:
				doc.openPoem();
				break;
			case strus::WikimediaLexem::ClosePoem:
				doc.closePoem();
				break;
			case strus::WikimediaLexem::OpenCitation:
				doc.openCitation( lexem.value);
				break;
			case strus::WikimediaLexem::CloseCitation:
				doc.closeCitation();
				break;
			case strus::WikimediaLexem::OpenWWWLink:
				doc.openWebLink( lexem.value);
				break;
			case strus::WikimediaLexem::CloseWWWLink:
				doc.closeWebLink();
				break;
			case strus::WikimediaLexem::OpenPageLink:
			{
				std::pair<std::string,std::string> lnk = strus::LinkMap::getLinkParts( lexem.value);
				if (g_linkmap)
				{
					const char* val = g_linkmap->get( lnk.first);
					if (val)
					{
						doc.openPageLink( val, lnk.second);
					}
					else
					{
						doc.addError( strus::string_format( "failed to resolve page link '%s'", lexem.value.c_str()));
						doc.openPageLink( lnk.first, lnk.second);
					}
				}
				else
				{
					doc.openPageLink( lnk.first, lnk.second);
				}
				break;
			}
			case strus::WikimediaLexem::ClosePageLink:
				doc.closePageLink();
				break;
			case strus::WikimediaLexem::OpenTable:
				doc.openTable();
				break;
			case strus::WikimediaLexem::CloseTable:
				doc.closeOpenEolnItem();
				doc.closeTable();
				break;
			case strus::WikimediaLexem::TableTitle:
				doc.closeOpenEolnItem();
				doc.implicitOpenTableIfUndefined();
				doc.addTableTitle();
				break;
			case strus::WikimediaLexem::TableHeadDelim:
			{
				doc.closeOpenEolnItem();
				doc.implicitOpenTableIfUndefined();
				int colspan = lexem.colspan();
				if (colspan <= 0)
				{
					doc.addError( "invalid colspan attribute value");
					colspan = 0;
				}
				int rowspan = lexem.rowspan();
				if (rowspan <= 0)
				{
					doc.addError( "invalid colspan attribute value");
					rowspan = 0;
				}
				doc.addTableHead( rowspan, colspan);
				break;
			}
			case strus::WikimediaLexem::TableRowDelim:
				doc.closeOpenEolnItem();
				doc.implicitOpenTableIfUndefined();
				doc.addTableRow();
				break;
			case strus::WikimediaLexem::TableColDelim:
			{
				doc.closeOpenEolnItem();
				strus::Paragraph::StructType tp = doc.currentStructType();
				if (tp == strus::Paragraph::StructPageLink
				||  tp == strus::Paragraph::StructWebLink)
				{
					doc.clearOpenText();
					//... ignore last text and restart structure
				}
				else
				if (tp == strus::Paragraph::StructCitation
				||  tp == strus::Paragraph::StructRef
				||  tp == strus::Paragraph::StructAttribute)
				{
					doc.addAttribute( lexem.value);
				}
				else if (tp == strus::Paragraph::StructNone)
				{
					doc.openListItem( 1);
				}
				else
				{
					int colspan = lexem.colspan();
					if (colspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						colspan = 0;
					}
					int rowspan = lexem.rowspan();
					if (rowspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						rowspan = 0;
					}
					doc.addTableCell( rowspan, colspan);
				}
				break;
			}
			case strus::WikimediaLexem::ColDelim:
			{
				doc.closeOpenQuoteItems();
				strus::Paragraph::StructType tp = doc.currentStructType();
				if (tp == strus::Paragraph::StructPageLink
				||  tp == strus::Paragraph::StructWebLink)
				{
					doc.clearOpenText();
					//... ignore last text and restart structure
				}
				else if (tp == strus::Paragraph::StructList)
				{
					doc.addText( " |");
					//... ignore
				}
				else if (tp == strus::Paragraph::StructTableTitle)
				{
					doc.addTableTitle();
				}
				else if (tp == strus::Paragraph::StructTableHead
					|| tp == strus::Paragraph::StructTableCell)
				{
					int colspan = lexem.colspan();
					if (colspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						colspan = 0;
					}
					int rowspan = lexem.rowspan();
					if (rowspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						rowspan = 0;
					}
					doc.repeatTableCell( rowspan, colspan);
				}
				else
				{
					doc.addAttribute( lexem.value);
				}
				break;
			}
			case strus::WikimediaLexem::DoubleColDelim:
			{
				doc.closeOpenQuoteItems();
				strus::Paragraph::StructType tp = doc.currentStructType();
				if (tp == strus::Paragraph::StructPageLink
				||  tp == strus::Paragraph::StructWebLink)
				{
					doc.clearOpenText();
					//... ignore last text and restart structure
				}
				else if (tp == strus::Paragraph::StructTableTitle)
				{
					doc.addTableTitle();
				}
				else if (tp == strus::Paragraph::StructTableHead
					|| tp == strus::Paragraph::StructTableCell)
				{
					int colspan = lexem.colspan();
					if (colspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						colspan = 0;
					}
					int rowspan = lexem.rowspan();
					if (rowspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						rowspan = 0;
					}
					doc.repeatTableCell( rowspan, colspan);
				}
				else if (tp == strus::Paragraph::StructCitation || tp == strus::Paragraph::StructAttribute)
				{
					doc.addAttribute( lexem.value);
				}
				else if (tp == strus::Paragraph::StructTable)
				{
					int colspan = lexem.colspan();
					if (colspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						colspan = 0;
					}
					int rowspan = lexem.rowspan();
					if (rowspan <= 0)
					{
						doc.addError( "invalid colspan attribute value");
						rowspan = 0;
					}
					doc.addTableCell( rowspan, colspan);
				}
				else
				{
					doc.addError( "unexpected token '||'");
				}
			}
		}
		if (doc.hasNewErrors())
		{
			doc.setErrorsSourceInfo( lexer.currentSourceExtract( 60));
		}
		
	}
}

static void createOutputDir( int fileCounter)
{
	char dirnam[ 16];
	std::snprintf( dirnam, sizeof(dirnam), "%04u", fileCounter / 1000);
	std::string dirpath( strus::joinFilePath( g_outputdir, dirnam));
	int ec = strus::createDir( dirpath, false/*fail if exists*/);
	if (ec) std::cerr << "error creating directory " << dirpath << ": " << std::strerror(ec) << std::endl;
}

static void writeWorkFile( int fileCounter, const std::string& docid, const std::string& extension, const std::string& content)
{
	char dirnam[ 16];
	std::snprintf( dirnam, sizeof(dirnam), "%04u", fileCounter / 1000);
	int ec;

	std::string filename( strus::joinFilePath( strus::joinFilePath( g_outputdir, dirnam), docid + extension));
	ec = strus::writeFile( filename, content);
	if (ec) std::cerr << "error writing file " << filename << ": " << std::strerror(ec) << std::endl;
}

static void removeWorkFile( int fileCounter, const std::string& docid, const std::string& extension)
{
	char dirnam[ 16];
	std::snprintf( dirnam, sizeof(dirnam), "%04u", fileCounter / 1000);
	int ec;

	std::string filename( strus::joinFilePath( strus::joinFilePath( g_outputdir, dirnam), docid + extension));
	ec = strus::removeFile( filename, false);
	if (ec) std::cerr << "error removing file " << filename << ": " << std::strerror(ec) << std::endl;
}

static void writeErrorFile( int fileCounter, const std::string& docid, const std::string& errorstext)
{
	writeWorkFile( fileCounter, docid, ".err", errorstext);
	if (g_verbosity >= 1) std::cerr << "got error:" << std::endl << errorstext << std::endl;
}

static void writeFatalErrorFile( int fileCounter, const std::string& docid, const std::string& errorstext)
{
	writeWorkFile( fileCounter, docid, ".fatal.err", errorstext);
	if (g_verbosity >= 1) std::cerr << "got fatal error:" << std::endl << errorstext << std::endl;
}

static void writeInputFile( int fileCounter, const std::string& docid, const std::string& title, const std::string& content)
{
	std::string origxml = strus::DocumentStructure::getInputXML( title, content);
	writeWorkFile( fileCounter, docid, ".orig.xml", origxml);
}

static void writeLexerDumpFile( int fileCounter, const strus::DocumentStructure& doc)
{
	writeWorkFile( fileCounter, doc.id(), ".txt", doc.tostring());
}

static void writeOutputFiles( int fileCounter, const strus::DocumentStructure& doc)
{
	writeWorkFile( fileCounter, doc.id(), ".xml", doc.toxml( g_beautified));
	std::string strange = doc.reportStrangeFeatures();
	if (strange.empty())
	{
		removeWorkFile( fileCounter, doc.id(), ".strange.txt");
	}
	else
	{
		writeWorkFile( fileCounter, doc.id(), ".strange.txt", strange);
	}
	if (doc.errors().empty())
	{
		removeWorkFile( fileCounter, doc.id(), ".err");
	}
	else
	{
		std::ostringstream errorstext;
		std::vector<std::string>::const_iterator ei = doc.errors().begin(), ee = doc.errors().end();
		for (int eidx=1; ei != ee; ++ei,++eidx)
		{
			errorstext << "[" << eidx << "] " << *ei << "\n";
		}
		std::string errdump( errorstext.str());
		writeWorkFile( fileCounter, doc.id(), ".err", errdump);
		if (g_verbosity >= 1) std::cerr << "got errors:" << std::endl << errdump << std::endl;
	}
}

class Work
{
public:
	Work()
		:m_writeDumpsAlways(false),m_fileindex(-1),m_title(),m_content(){}
	Work( int fileindex_, const std::string& title_, const std::string& content_, bool writeDumpsAlways_)
		:m_writeDumpsAlways(writeDumpsAlways_),m_fileindex(fileindex_),m_title(title_),m_content(content_){}
	Work( const Work& o)
		:m_writeDumpsAlways(o.m_writeDumpsAlways),m_fileindex(o.m_fileindex),m_title(o.m_title),m_content(o.m_content){}

	bool empty() const
	{
		return m_content.empty();
	}
	int fileindex() const				{return m_fileindex;}
	const std::string& title() const		{return m_title;}
	const std::string& content() const		{return m_content;}

	void process()
	{
		bool inputFileWritten = false;
		strus::DocumentStructure doc;
		doc.setTitle( m_title);
		try
		{
			if (!g_origOutputPattern.empty() && 0!=std::strstr( m_title.c_str(), g_origOutputPattern.c_str()))
			{
				writeInputFile( m_fileindex, doc.id(), m_title, m_content);
				inputFileWritten = true;
			}
			parseDocumentText( doc, m_content.c_str(), m_content.size());
			doc.finish();
			writeOutputFiles( m_fileindex, doc);
			if (m_writeDumpsAlways || !doc.errors().empty())
			{
				writeLexerDumpFile( m_fileindex, doc);
				if (!inputFileWritten)
				{
					writeInputFile( m_fileindex, doc.id(), m_title, m_content);
					inputFileWritten = true;
				}
			}
		}
		catch (const std::runtime_error& err)
		{
			writeLexerDumpFile( m_fileindex, doc);
			writeErrorFile( m_fileindex, doc.id(), err.what());
			writeFatalErrorFile( m_fileindex, doc.id(), std::string(err.what()) + "\n");
			if (!inputFileWritten)
			{
				writeInputFile( m_fileindex, doc.id(), m_title, m_content);
				inputFileWritten = true;
			}
		}
	}

private:
	bool m_writeDumpsAlways;
	int m_fileindex;
	std::string m_title;
	std::string m_content;
};

class Worker
{
public:
	Worker()
		:m_thread(0),m_threadid(0),m_terminated(false),m_eof(false),m_writeDumpsAlways(g_dumps){}
	~Worker()
	{
		waitTermination();
	}

	void push( int filecounter, const std::string& title, const std::string& content)
	{
		strus::unique_lock lock( m_queue_mutex);
		m_queue.push( Work( filecounter, title, content, m_writeDumpsAlways));
		m_cv.notify_one();
	}
	void terminate()
	{
		m_terminated.set( true);
		m_cv.notify_all();
	}
	void waitTermination()
	{
		if (m_thread)
		{
			m_eof.set( true);
			m_cv.notify_all();
			m_thread->join();
			delete m_thread;
			m_thread = 0;
			if (g_verbosity >= 1) std::cerr << strus::string_format( "thread %d terminated\n", m_threadid) << std::flush;
		}
	}
	void waitSignal()
	{
		strus::unique_lock lock( m_cv_mutex);
		m_cv.wait( lock);
	}
	bool fetch( Work& work)
	{
		strus::unique_lock lock( m_queue_mutex);
		if (m_queue.empty())
		{
			if (m_eof.test())
			{
				m_terminated.set( true);
			}
			return false;
		}
		else
		{
			work = m_queue.front();
			m_queue.pop();
			return true;
		}
	}

	void run()
	{
		if (g_verbosity >= 1) std::cerr << strus::string_format( "thread %d started\n", m_threadid) << std::flush;
		while (!m_terminated.test())
		{
			std::string title;
			try
			{
				waitSignal();
				Work work;
				while (fetch( work))
				{
					title = work.title();
					if (g_verbosity >= 1) std::cerr << strus::string_format( "thread %d process document '%s'\n", m_threadid, title.c_str()) << std::flush;
					work.process();
				}
			}
			catch (const std::bad_alloc&)
			{
				std::cerr << "out of memory processing document " << title << std::endl;
			}
			catch (const std::runtime_error& err)
			{
				std::cerr << "error processing document " << title << ": " << err.what() << std::endl;
			}
		}
	}
	void start( int threadid_)
	{
		m_threadid = threadid_;
		if (m_thread) throw std::runtime_error("start called twice");
		m_thread = new strus::thread( &Worker::run, this);
	}

private:
	strus::condition_variable m_cv;
	strus::mutex m_cv_mutex;
	strus::mutex m_queue_mutex;
	std::queue<Work> m_queue;
	strus::thread* m_thread;
	int m_threadid;
	strus::AtomicFlag m_terminated;
	strus::AtomicFlag m_eof;
	bool m_writeDumpsAlways;
};

class IStream
	:public textwolf::IStream
{
public:
	explicit IStream( const std::string& docpath)
		:m_impl(docpath){}
	virtual ~IStream(){}

	virtual std::size_t read( void* buf, std::size_t bufsize)
	{
		return m_impl.read( (char*)buf, bufsize);
	}

	virtual int errorcode() const
	{
		return m_impl.error();
	}

private:
	strus::InputStream m_impl;
};

static int getUIntOptionArg( int argi, int argc, const char* argv[])
{
	if (argv[argi+1])
	{
		return strus::numstring_conv::touint( argv[argi+1], std::numeric_limits<int>::max());
	}
	else
	{
		throw std::runtime_error( std::string("no argument given for option ") + argv[argi]);
	}
}

enum TagId {TagIgnored,TagPage,TagNs,TagTitle,TagText,TagRedirect};


int main( int argc, const char* argv[])
{
	int rt = 0;
	try
	{
		int argi = 1;
		int nofThreads = 0;
		std::set<int> namespacemap;
		bool namespaceset = false;
		bool printusage = false;
		bool collectRedirects = false;
		std::string linkmapfilename;

		for (;argi < argc; ++argi)
		{
			if (0==std::strcmp(argv[argi],"-VV"))
			{
				g_verbosity += 2;
			}
			else if (0==std::strcmp(argv[argi],"-V"))
			{
				++g_verbosity;
			}
			else if (0==std::strcmp(argv[argi],"-B"))
			{
				g_beautified = true;
			}
			else if (0==std::strcmp(argv[argi],"-D"))
			{
				g_dumps = true;
			}
			else if (0==std::strcmp(argv[argi],"-h"))
			{
				printusage = true;
			}
			else if (0==std::memcmp(argv[argi],"-S",2))
			{
				if (g_breakpoint > 0) throw std::runtime_error("duplicated option -S <lexemid>");
				g_breakpoint = getUIntOptionArg( argi, argc, argv);
				++argi;
			}
			else if (0==std::memcmp(argv[argi],"-O",2))
			{
				if (!g_origOutputPattern.empty()) throw std::runtime_error("duplicated option -O <substr>");
				++argi;
				if (argi == argc) throw std::runtime_error("option -O without argument");
				g_origOutputPattern = argv[ argi];
			}
			else if (0==std::memcmp(argv[argi],"-L",2))
			{
				if (!linkmapfilename.empty()) throw std::runtime_error("duplicate or conflicting option -L <linkmapfile> or -R <linkmapfile>");
				++argi;
				if (argi == argc) throw std::runtime_error("option -L without argument");
				linkmapfilename = argv[ argi];
			}
			else if (0==std::memcmp(argv[argi],"-R",2))
			{
				if (!linkmapfilename.empty()) throw std::runtime_error("duplicate or conflicting option -L <linkmapfile> or -R <linkmapfile>");
				++argi;
				if (argi == argc) throw std::runtime_error("option -R without argument");
				linkmapfilename = argv[ argi];
				collectRedirects = true;
			}
			else if (0==std::memcmp(argv[argi],"-n",2))
			{
				namespaceset = true;
				namespacemap.insert( getUIntOptionArg( argi, argc, argv));
				++argi;
			}
			else if (0==std::memcmp(argv[argi],"-t",2))
			{
				nofThreads = getUIntOptionArg( argi, argc, argv);
				++argi;
			}
			else if (argv[argi][0] == '-' && argv[argi][1] == '-')
			{
				++argi;
				break;
			}
			else if (argv[argi][0] == '-' && argv[argi][1])
			{
				std::cerr << "unknown option '" << argv[argi] << "'" << std::endl;
				printusage = true;
			}
			else
			{
				break;
			}
		}
		if (argc > argi+2 || argc < argi+1)
		{
			if (argc > argi+2) std::cerr << "too many arguments" << std::endl;
			if (argc < argi+2) std::cerr << "too few arguments" << std::endl;
			printusage = true;
			rt = -1;
		}
		if (printusage)
		{
			std::cerr << "Usage: strusWikimediaToXml [options] <inputfile> [<outputdir>]" << std::endl;
			std::cerr << "<inputfile>   :File to process or '-' for stdin" << std::endl;
			std::cerr << "<outputdir>   :Directory where output files and directories are written to." << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "    -h           :print this usage" << std::endl;
			std::cerr << "    -V           :verbosity level 1 (output document title errors to stderr)" << std::endl;
			std::cerr << "    -VV          :verbosity level 2 (output lexems found additional to level 1)" << std::endl;
			std::cerr << "    -S <lexemid> :stop verbose output (option -VV) at lexem with\n";
			std::cerr << "                  index <lexemid> (continue processing)" << std::endl;
			std::cerr << "    -O <substr>  :write dump file for documents with a title\n";
			std::cerr << "                  containing <substr> as title sub string" << std::endl;
			std::cerr << "    -B           :beautified readable XML output" << std::endl;
			std::cerr << "    -D           :write dump files always, not only in case of an error" << std::endl;
			std::cerr << "    -t <threads> :number of threads to use is <threads>" << std::endl;
			std::cerr << "    -n <ns>      :reduce output to namespace <ns> (0=article)" << std::endl;
			std::cerr << "    -R <lnkfile> :collect redirects only and write them to <lnkfile>" << std::endl;
			std::cerr << "    -L <lnkfile> :Load link file <lnkfile> for verifying page links" << std::endl;
			std::cerr << std::endl;
			std::cerr << "Description:" << std::endl;
			std::cerr << "  Takes a unpacked Wikipedia XML dump as input and tries to convert it to\n";
			std::cerr << "    a set of XML files in a schema suitable for information retrieval." << std::endl;
			std::cerr << "  The produced XML document files have the extension .xml and are written\n";
			std::cerr << "    into a subdirectory of <outputdir>. The subdirectories for the output are\n";
			std::cerr << "    enumerated with 4 digits in ascending order starting with 0000." << std::endl;
			std::cerr << "  Each subdirectory contains at maximum 1000 <docid>.xml output files." << std::endl;
			std::cerr << "  Each output file contains one document and has an identifier derived\n";
			std::cerr << "    from the Wikipedia title." << std::endl;
			std::cerr << "  Besides the <docid>.xml files, the following files are written:" << std::endl;
			std::cerr << "    <docid>.err         :File with recoverable errors in the document" << std::endl;
			std::cerr << "    <docid>.fatal.err   :File with an exception thrown while processing" << std::endl;
			std::cerr << "    <docid>.strange.txt :File listing some suspicious text elements\n";
			std::cerr << "                         This list is useful for tracking classification\n";
			std::cerr << "                         errors." << std::endl;
			std::cerr << "    <docid>.orig.xml    :File with a dump of the document processed\n";
			std::cerr << "                         (only written if required or on error)" << std::endl;
			std::cerr << std::endl;
			std::cerr << "Output XML format:" << std::endl;
			std::cerr << "  The tag hierarchy is as best effort intendet to be as flat as possible.\n";
			std::cerr << "  The following list explains the tags in the output:\n";
			std::cerr << "     <docid>            :The content specifies a unique document identifier\n";
			std::cerr << "                         (the title with '_' instead of ' ' and some other encodings)\n";
			std::cerr << "     <entity>           :A marked entity in the Wikipedia collection\n";
			std::cerr << "     <quot>             :A quoted string in the Wikipedia collection\n";
			std::cerr << "     <heading id='h#'>  :A subtitle or heading in a document\n";
			std::cerr << "     <list id='l#'>     :A list item in a document\n";
			std::cerr << "     <text>             :A text item in a document\n";
			std::cerr << "     <attr>             :An attribute in a document\n";
			std::cerr << "     <citation>         :A citation in a document\n";
			std::cerr << "     <ref>              :A reference structure in a document\n";
			std::cerr << "     <pagelink>         :A link to a page in the collection\n";
			std::cerr << "     <weblink>          :A web link to a page in the internet\n";
			std::cerr << "     <table>            :A table implementation\n";
			std::cerr << "     <tabtitle>         :Sub-title text in the table\n";
			std::cerr << "     <head id='C#'>     :head cells of a table adressing a column cell\n";
			std::cerr << "     <head id='R#'>     :head cells of a table adressing a row cell\n";
			std::cerr << "     <cell id='R#'>     :Data sells of a table with a list of identifiers making it addressable\n";
			std::cerr << "     <text>             :Indexable text\n";
			std::cerr << "     <char>             :Content is on or a sequence of special characters\n";
			std::cerr << "     <code>             :Text descibing some sort of an Id not suitable for retrival\n";
			std::cerr << "     <math>             :Text marked as LaTeX syntax math formula\n";
			std::cerr << "     <time>             :A timestimp of the form \"YYMMDDThhmmss<zone>\", <zone> = Z (UTC)\n";
			std::cerr << "     <bibref>           :bibliographic reference\n";
			std::cerr << "     <nowiki>           :information not to index\n";
			std::cerr << "     <citlink>          :internal link to a citation in this document\n";
			std::cerr << "     <reflink>          :internal link to a reference in this document\n";
			std::cerr << std::endl;
			return rt;
		}
		IStream input( argv[argi]);
		if (argi+1 < argc) g_outputdir = argv[argi+1];
		textwolf::IStreamIterator inputiterator( &input, 1<<16/*buffer size*/);
		if (nofThreads <= 0) nofThreads = 0;
		strus::local_ptr<strus::LinkMap> linkmap;
		strus::LinkMapBuilder linkmapBuilder;
		if (!linkmapfilename.empty())
		{
			linkmap.reset( new strus::LinkMap());
			if (!collectRedirects)
			{
				linkmap->load( strus::joinFilePath( g_outputdir, linkmapfilename));
				g_linkmap = linkmap.get();
			}
		}

		struct WorkerArray
		{
			WorkerArray( Worker* ar_)
				:ar(ar_){}
			~WorkerArray()
			{
				delete [] ar;
			}
			Worker* ar;
		};
		WorkerArray workers( nofThreads ? new Worker[ nofThreads] : 0);
		for (int wi=0; wi < nofThreads; ++wi)
		{
			workers.ar[ wi].start( wi+1);
		}

		bool terminated = false;
		struct DocAttributes
		{
			int ns;
			std::string title;
			std::string redirect_title;
			std::string content;

			DocAttributes()
				:ns(0),title(),redirect_title(){}
			void clear()
			{
				ns = 0;
				title.clear();
				redirect_title.clear();
				content.clear();
			}
		};

		XmlScanner xs( inputiterator);
		XmlScanner::iterator itr=xs.begin(),end=xs.end();
		DocAttributes docAttributes;
		int workeridx = 0;
		int docCounter = 0;
		TagId lastTag = TagIgnored;
		std::vector<TagId> tagstack;

		for (; !terminated && itr!=end; ++itr)
		{
			if (g_verbosity >= 2) std::cout << "XML " << itr->name() << " " << strus::outputLineString( itr->content(), itr->content()+itr->size(), 80) << std::endl;
			switch (itr->type())
			{
				case XmlScanner::None: break;
				case XmlScanner::ErrorOccurred: throw std::runtime_error( itr->content());
				case XmlScanner::HeaderStart:/*no break!*/
				case XmlScanner::HeaderAttribName:/*no break!*/
				case XmlScanner::HeaderAttribValue:/*no break!*/
				case XmlScanner::HeaderEnd:/*no break!*/
				case XmlScanner::DocAttribValue:/*no break!*/
				case XmlScanner::DocAttribEnd:/*no break!*/
					break;
				case XmlScanner::TagAttribName:
				{
					if (lastTag == TagRedirect)
					{
						if (itr->size() == 5 && 0==std::memcmp( itr->content(), "title", itr->size()))
						{
							++itr;
							if (itr->type() == XmlScanner::TagAttribValue)
							{
								docAttributes.redirect_title = std::string( itr->content(), itr->size());
							}
						}
					}
					break;
				}
				case XmlScanner::TagAttribValue:
				{
					break;
				}
				case XmlScanner::OpenTag: 
				{
					lastTag = TagIgnored;
					if (namespaceset && itr->size() == 2  && 0==std::memcmp( itr->content(), "ns", itr->size()))
					{
						lastTag = TagNs;
					}
					else if (itr->size() == 4 && 0==std::memcmp( itr->content(), "page", itr->size()))
					{
						lastTag = TagPage;
						docAttributes.clear();
						if (docCounter % 1000 == 0 && !collectRedirects)
						{
							createOutputDir( docCounter);
						}
					}
					else if (itr->size() == 5 && 0==std::memcmp( itr->content(), "title", itr->size()))
					{
						lastTag = TagTitle;
					}
					if (itr->size() == 4 && 0==std::memcmp( itr->content(), "text", itr->size()))
					{
						lastTag = TagText;
					}
					if (itr->size() == 8 && 0==std::memcmp( itr->content(), "redirect", itr->size()))
					{
						lastTag = TagRedirect;
					}
					tagstack.push_back( lastTag);
					break;
				}
				case XmlScanner::CloseTagIm:
				case XmlScanner::CloseTag:
				{
					lastTag = TagIgnored;
					TagId closedTag = TagIgnored;
					if (!tagstack.empty())
					{
						closedTag = tagstack.back();
						tagstack.pop_back();
					}
					if (closedTag == TagPage)
					{
						if (namespaceset && namespacemap.find( docAttributes.ns) == namespacemap.end())
						{
							//... ignore document but those with ns set to what is selected by option '-n'
						}
						else if (!docAttributes.redirect_title.empty() && docAttributes.content.size() < 1000)
						{
							if (collectRedirects)
							{
								std::pair<std::string,std::string> redir_parts = strus::LinkMap::getLinkParts( docAttributes.redirect_title);
								if (g_verbosity >= 1) std::cerr << strus::string_format( "%s => %s\n", docAttributes.title.c_str(), docAttributes.redirect_title.c_str());
								linkmapBuilder.redirect( docAttributes.title, redir_parts.first);
							}
						}
						else if (!docAttributes.title.empty() && !docAttributes.content.empty())
						{
							if (collectRedirects)
							{
								if (g_verbosity >= 1) std::cerr << strus::string_format( "link %s => %s\n", docAttributes.title.c_str(), docAttributes.title.c_str());
								linkmapBuilder.define( docAttributes.title);
							}
							else
							{
								const char* normtitle = g_linkmap ? g_linkmap->get( docAttributes.title) : 0;
								if (!normtitle) normtitle = docAttributes.title.c_str();
								if (nofThreads)
								{
									workeridx = docCounter % nofThreads;
									workers.ar[ workeridx].push( docCounter, normtitle, docAttributes.content);
									++docCounter;
									if (docCounter % 1000 == 0)
									{
										std::cerr << "processed " << docCounter << " documents" << std::endl;
									}
								}
								else
								{
									try
									{
										Work work( docCounter, normtitle, docAttributes.content, g_dumps);
										if (g_verbosity >= 1) std::cerr << strus::string_format( "process document '%s'\n", docAttributes.title.c_str()) << std::flush;
										work.process();
									} 
									catch (const std::bad_alloc&)
									{
										std::cerr << "out of memory processing document " << docAttributes.title << std::endl;
									}
									catch (const std::runtime_error& err)
									{
										std::cerr << "error processing document " << docAttributes.title << ": " << err.what() << std::endl;
									}
									++docCounter;
									if (docCounter % 1000 == 0)
									{
										std::cerr << "processed " << docCounter << " documents" << std::endl;
									}
								}
							}
						}
						else
						{
							std::cerr << "invalid document" << std::endl;
						}
					}
					break;
				}
				case XmlScanner::Content:
					switch (lastTag)
					{
						case TagIgnored:
							break;
						case TagPage:
							break;
						case TagNs:
						{
							std::string contentstr( itr->content(), itr->size());
							docAttributes.ns = strus::numstring_conv::toint( contentstr, 10000);
							break;
						}
						case TagTitle:
						{
							docAttributes.title = std::string( itr->content(), itr->size());
							break;
						}
						case TagText:
						{
							docAttributes.content = std::string( itr->content(), itr->size());
							break;
						}
						case TagRedirect:
						{
							docAttributes.redirect_title = std::string( itr->content(), itr->size());
							break;
						}
					}
					break;
				case XmlScanner::Exit:
					terminated = true;
					break;
			}
		}
		for (int wi=0; wi < nofThreads; ++wi)
		{
			workers.ar[ wi].waitTermination();
		}
		if (collectRedirects)
		{
			std::string linkoutfilename( strus::joinFilePath( g_outputdir, linkmapfilename));
			std::cerr << "links are written to " << linkoutfilename << "..." << std::endl;
			linkmap.reset( new strus::LinkMap( linkmapBuilder.build()));
			linkmap->write( linkoutfilename);
		}
		return rt;
	}
	catch (const std::runtime_error& e)
	{
		std::cerr << "ERROR " << e.what() << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cerr << "EXCEPTION " << e.what() << std::endl;
	}
	return -1;
}


