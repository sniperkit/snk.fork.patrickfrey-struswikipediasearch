/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "textwolf/istreamiterator.hpp"
#include "textwolf/xmlscanner.hpp"
#include "textwolf/xmlprinter.hpp"
#include "textwolf/charset_utf8.hpp"
#include "inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <string>
#include <stdexcept>
#include <memory>
#include <vector>
#include <set>
#include <limits>

#undef STRUS_LOWLEVEL_DEBUG

typedef textwolf::XMLScanner<textwolf::IStreamIterator,textwolf::charset::UTF8,textwolf::charset::UTF8,std::string> XmlScanner;
typedef textwolf::XMLPrinter<textwolf::charset::UTF8,textwolf::charset::UTF8,std::string> XmlPrinterBase;

bool g_silent = false;
#define OUT_WARNING	if(!g_silent)std::cerr<<"WARNING "


static std::string outputString( const char* si, const char* se)
{
	if (!si || !se)
	{
		throw std::runtime_error( "outputString called with NULL argument");
	}
	else if (se - si > 60)
	{
		return std::string( si, 30) + "..." + std::string( se-30, 30);
	}
	else
	{
		return std::string( si, se-si);
	}
}

class XmlPrinter
	:public XmlPrinterBase
{
public:
	XmlPrinter( bool subDocument=false)
		:XmlPrinterBase(subDocument){}

	void printOpenTag( const std::string& tag, std::string& buf)
	{
		if (!XmlPrinterBase::printOpenTag( tag.c_str(), tag.size(), buf))
		{
			const char* errstr = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (errstr?errstr:"") + " when printing open tag: " + outputString(tag.c_str(),tag.c_str()+tag.size()));
		}
	}

	void printOpenTag( const char* name, std::string& buf)
	{
		if (!XmlPrinterBase::printOpenTag( name, std::strlen(name), buf))
		{
			const char* errstr = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (errstr?errstr:"") + " when printing open tag: " + outputString(name,name+std::strlen(name)));
		}
	}

	void printAttribute( const std::string& name, std::string& buf)
	{
		if (!XmlPrinterBase::printAttribute( name.c_str(), name.size(), buf))
		{
			const char* errstr = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (errstr?errstr:"") + " when printing attribute: " + outputString(name.c_str(),name.c_str()+name.size()));
		}
	}

	void printAttribute( const char* name, std::string& buf)
	{
		if (!XmlPrinterBase::printAttribute( name, std::strlen(name), buf))
		{
			const char* errstr = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (errstr?errstr:"") + " when printing attribute: " + outputString(name,name+std::strlen(name)));
		}
	}

	void printValue( const std::string& val, std::string& buf)
	{
		if (!XmlPrinterBase::printValue( val.c_str(), val.size(), buf))
		{
			const char* err = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (err?err:"") + " when printing: " + outputString(val.c_str(),val.c_str()+val.size()));
		}
	}

	void printValue( const char* si, const char* se, std::string& buf)
	{
		if (!XmlPrinterBase::printValue( si, se-si, buf))
		{
			const char* err = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (err?err:"") + " when printing: " + outputString(si,se));
		}
	}

	void switchToContent( std::string& buf)
	{
		if (!XmlPrinterBase::printValue( "", 0, buf))
		{
			const char* err = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (err?err:"") + " when switching to content");
		}
	}

	void printCloseTag( std::string& buf)
	{
		if (!XmlPrinterBase::printCloseTag( buf))
		{
			const char* err = XmlPrinterBase::lasterror();
			throw std::runtime_error( std::string( "xml print error: ") + (err?err:"") + " when printing close tag");
		}
	}
};


static bool isAlpha( char ch)
{
	return (ch|32) >= 'a' && (ch|32) <= 'z';
}

static bool isDigit( char ch)
{
	return (ch) >= '0' && (ch) <= '9';
}

static bool isAlphaNum( char ch)
{
	return isAlpha(ch) || isDigit(ch);
}

static bool isSpace( char ch)
{
	return ((unsigned char)ch <= 32);
}

static bool isEqual( const char* id, const char* src, std::size_t size)
{
	std::size_t ii=0;
	for (; ii<size && (id[ii]|32)==(src[ii]|32); ++ii){}
	return ii==size;
}

static bool isAttributeString( const std::string& src)
{
	if (src.size() > 80) return false;
	std::string::const_iterator si = src.begin(), se = src.end();
	for (; si != se && (*si == ' ' || *si == '-' || *si == '_' || isAlphaNum(*si)); ++si){}
	return (si == se);
}

static std::string makeAttributeName( const std::string& src)
{
	std::string rt;
	std::string::const_iterator si = src.begin(), se = src.end();
	for (; si != se && (*si == ' ' || *si == '-' || *si == '_' || isAlphaNum(*si)); ++si)
	{
		if (isAlpha(*si))
		{
			rt.push_back( *si|32);
		}
		else if (isDigit(*si))
		{
			rt.push_back( *si);
		}
		else if (*si == '_' || *si == '-')
		{
			rt.push_back( '_');
		}
	}
	return rt;
}

static bool isEmpty( const std::string& src)
{
	if (src.size() > 80) return false;
	std::string::const_iterator si = src.begin(), se = src.end();
	for (; si != se && isSpace(*si); ++si){}
	return (si == se);
}

static std::string trim( const std::string& src)
{
	char const* si = src.c_str();
	const char* se = src.c_str() + src.size();
	while (si < se && isSpace(*si)) ++si;
	while (si < se && isSpace(*(se-1))) --se;
	return std::string( si, (std::size_t)(se-si));
}

static const char* findPattern( char const* si, const char* se, const char* pattern)
{
	int plen = std::strlen( pattern);
	for (;;)
	{
		const char* rt = (const char*)std::memchr( si, pattern[0], se-si);
		if (!rt) return 0;
		if (se - rt >= plen && 0==std::memcmp( rt, pattern, plen))
		{
			return rt + plen;
		}
		si = rt+1;
	}
}

static const char* skipToEndTag( const char* tagname, char const* si, const char* se)
{
	std::size_t tagnamelen = std::strlen( tagname);
	const char* end = findPattern( si+1, se, "</");
	if (!end || 0!=std::memcmp( tagname, end, tagnamelen))
	{
		OUT_WARNING << "unclosed tag '" << tagname << "': " << outputString( si, se) << std::endl;
		return si+tagnamelen;
	}
	else
	{
		return end;
	}
}

static const char* skipTag( char const* si, const char* se)
{
#ifdef STRUS_LOWLEVEL_DEBUG
	std::cerr << "SKIP TAG '" << outputString( si, se) << "'" << std::endl;
#endif
	const char* start = si++;
	if (si < se && si[0] == '!' && si[1] == '-' && si[2] == '-')
	{
		const char* end = findPattern( si+2, se, "-->");
		if (!end)
		{
			OUT_WARNING << "comment not closed:" << outputString( start, se) << std::endl;
			return si+3;
		}
		else
		{
			return end;
		}
	}
	else
	{
		const char* tgnam = si;
		if (!isAlpha(*si) && *si != '/') return si;
		for (; si < se && *si != '>'; ++si){}
		if (0==std::memcmp( tgnam, "nowiki", si-tgnam))
		{
			return skipToEndTag( "nowiki", si, se);
		}
		else if (0==std::memcmp( tgnam, "math", si-tgnam))
		{
			return skipToEndTag( "math", si, se);
		}
		else
		{
			return si+1;
		}
	}
}

class Lexer
{
public:
	enum LexemId
	{
		LexemEOF,
		LexemText,
		LexemRedirect,
		LexemHeading1,
		LexemHeading2,
		LexemHeading3,
		LexemHeading4,
		LexemHeading5,
		LexemHeading6,
		LexemListItem1,
		LexemListItem2,
		LexemListItem3,
		LexemListItem4,
		LexemEndOfLine,
		LexemOpenCitation,
		LexemCloseCitation,
		LexemOpenWWWLink,
		LexemOpenSquareBracket,
		LexemCloseSquareBracket,
		LexemOpenLink,
		LexemCloseLink,
		LexemOpenTable,
		LexemCloseTable,
		LexemTableRowDelim,
		LexemTableTitle,
		LexemTableHeadDelim,
		LexemColDelim
	};
	static const char* lexemIdName( LexemId lexemId)
	{
		static const char* ar[] =
		{
			"EOF","Text","Redirect","Heading1","Heading2","Heading3","Heading4",
			"Heading5","Heading6","ListItem1","ListItem2","ListItem3","ListItem4",
			"EndOfLine","OpenCitation","CloseCitation","OpenWWWLink", "OpenSquareBracket",
			"CloseSquareBracket","OpenLink","CloseLink",
			"OpenTable", "CloseTable","TableRowDelim","TableTitle",
			"TableHeadDelim","ColDelim"
		};
		return ar[lexemId];
	}

	struct Lexem
	{
		Lexem( LexemId id_, const std::string& value_)
			:id(id_),value(value_){}
		Lexem( LexemId id_)
			:id(id_),value(){}
		Lexem( const Lexem& o)
			:id(o.id),value(o.value){}

		LexemId id;
		std::string value;
	};

	Lexer( const char* src, std::size_t size)
		:m_prev_si(src),m_si(src),m_se(src+size){}

	Lexem next();
	std::string tryParseIdentifier( char assignop);
	std::string tryParseURL();
	bool eatFollowChar( char expectChr);

	std::string rest() const
	{
		return std::string( m_si, m_se-m_si);
	}

	std::string currentSourceExtract() const
	{
		return outputString( m_prev_si, m_si+20);
	}

private:
	char const* m_prev_si;
	char const* m_si;
	const char* m_se;
};


std::string Lexer::tryParseIdentifier( char assignop)
{
	std::string rt;
	char const* ti = m_si;
	while (ti < m_se && isSpace(*ti)) ++ti;
	if (ti < m_se && isAlpha(*ti))
	{
		while (ti < m_se && (isAlphaNum(*ti) || *ti == '-' || isSpace(*ti)))
		{
			char ch = *ti++;
			if (isAlpha(ch))
			{
				rt.push_back( ch|32);
			}
			else if (ch == '-')
			{
				rt.push_back( '-');
			}
			else if (rt.size()>0 && rt[ rt.size()-1] != ' ')
			{
				rt.push_back( ' ');
			}
		}
	}
	if (ti < m_se && *ti == assignop)
	{
		m_si = ++ti;
		return trim(rt);
	}
	else
	{
		return std::string();
	}
}

std::string Lexer::tryParseURL()
{
	char const* ti = m_si;
	while (ti < m_se && isSpace(*ti)) ++ti;
	while (ti < m_se && isAlpha(*ti)) ++ti;
	if (ti+2 < m_se && ti[0] == ':' && ti[1] == '/' && ti[2] == '/')
	{
		ti += 3;
		while (ti < m_se && (!isSpace(*ti) && *ti != ']')) ++ti;
		std::string linkid( m_si, ti-m_si);
		if (ti < m_se && isSpace(*ti))
		{
			m_si = ti+1;
		}
		else
		{
			m_si = ti;
		}
		return linkid;
	}
	return std::string();
}

bool Lexer::eatFollowChar( char expectChr)
{
	if (m_si < m_se && *m_si == expectChr)
	{
		++m_si;
		return true;
	}
	return false;
}

static char const* skipStyle( char const* si, char const* se)
{
	int sidx=0;
	char const* start = si;
	for (;si < se && *si > 0 && *si <= 32;++si){}
	for (;si < se && isAlpha(*si);++si,++sidx){}
	if (si < se && sidx > 0 && (*si == '=' || *si == ':'))
	{
		++si;
		if (si < se && (*si == '"' || *si == '\''))
		{
			char eb = *si;
			++si;
			for (; si < se && *si != '\n' && *si != eb; ++si){}
			if (si < se && *si == eb)
			{
				return skipStyle( si+1, se);
			}
		}
		else if (si < se && isAlphaNum(*si))
		{
			for (;si < se && isAlphaNum(*si);++si){}
			if (si < se)
			{
				return skipStyle( si, se);
			}
		}
	}
	return start;
}

Lexer::Lexem Lexer::next()
{
	m_prev_si = m_si;
	const char* start = m_si;
	while (m_si < m_se)
	{
		if (*m_si == '<')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			m_si = skipTag( m_si, m_se);
			return Lexem( LexemText, std::string( " ", 1));
		}
		else if (*m_si == '#')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			if (0==std::memcmp( "#REDIRECT", m_si, 9))
			{
				m_si += 9;
				return Lexem( LexemRedirect);
			}
			else
			{
				++m_si;
			}
		}
		else if (*m_si == '[')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			++m_si;
			if (m_si >= m_se) break;

			if (*m_si == '[')
			{
				++m_si;
				if (m_si < m_se && *m_si == '#')++m_si;
				std::string name = tryParseIdentifier( ':');
				return Lexem( LexemOpenLink, name);
			}
			else if (isAlpha(*m_si) || isSpace(*m_si))
			{
				std::string linkid = tryParseURL();
				if (linkid.size())
				{
					return Lexem( LexemOpenWWWLink, linkid);
				}
				else
				{
					return Lexem( LexemOpenSquareBracket);
				}
			}
			else
			{
				return Lexem( LexemOpenSquareBracket);
			}
		}
		else if (*m_si == '{')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			++m_si;
			if (m_si >= m_se) break;

			if (*m_si == '{')
			{
				++m_si;
				std::string name = tryParseIdentifier( '|');
				return Lexem( LexemOpenCitation, name);
			}
			else if (*m_si == '|')
			{
				m_si = skipStyle( m_si, m_se);
				return Lexem( LexemOpenTable);
			}
		}
		else if (*m_si == '|')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start) + " ");
			}
			++m_si;
			if (m_si >= m_se) break;

			if (*m_si == '-')
			{
				++m_si;
				m_si = skipStyle( m_si, m_se);
				return Lexem( LexemTableRowDelim);
			}
			else if (*m_si == '+')
			{
				++m_si;
				m_si = skipStyle( m_si, m_se);
				return Lexem( LexemTableTitle);
			}
			else if (*m_si == '}')
			{
				++m_si;
				return Lexem( LexemCloseTable);
			}
			else
			{
				m_si = skipStyle( m_si, m_se);
				return Lexem( LexemColDelim);
			}
		}
		else if (*m_si == '!' && m_si+1 < m_se && m_si[1] == '!')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			m_si += 2;
			m_si = skipStyle( m_si, m_se);
			return Lexem( LexemTableHeadDelim);
		}
		else if (*m_si == '\n')
		{
			while (m_si < m_se && isSpace(*m_si)) ++m_si;
			if (m_si < m_se)
			{
				if (*m_si == '!')
				{
					if (start < m_si-1)
					{
						--m_si;
						return Lexem( LexemText, std::string( start, m_si - start));
					}
					++m_si;
					m_si = skipStyle( m_si, m_se);
					return Lexem( LexemTableHeadDelim);
				}
				else if (*m_si == '|')
				{
					continue;
				}
				else if (*m_si == '*')
				{
					if (start < m_si-1)
					{
						--m_si;
						return Lexem( LexemText, std::string( start, m_si - start));
					}
					++m_si;
					int lidx = 0;
					for (; m_si != m_se && *m_si == '*'; ++lidx,++m_si){}
					if (lidx >= 4) lidx = 3;
					return Lexem( (LexemId)(LexemListItem1 + lidx));
				}
				else if (*m_si == '=')
				{
					if (start < m_si-1)
					{
						--m_si;
						return Lexem( LexemText, trim( std::string( start, m_si - start)));
					}
					char const* ti = ++m_si;
					int tcnt = 0;
					for (; ti != m_se && *ti == '='; ++tcnt,++ti){}
					if (tcnt <= 5 && tcnt >= 1)
					{
						start = ti;
						for (; ti < m_se && *ti != '=' && *ti != '\n'; ++ti){}
						if (ti < m_se && *ti == '=')
						{
							const char* end = m_si = ti;
							for (; m_si != m_se && *m_si == '='; ++m_si){}
							return Lexem( (LexemId)(LexemHeading1+(tcnt-1)), std::string( start, end-start));
						}
					}
				}
				else if (m_si+6 < m_se
					&& (isEqual("File:",m_si,5) || isEqual("Image:",m_si,6)))
				{
					m_si += 6;
					for (; m_si < m_se && *m_si != '\n' && *m_si != '|'; ++m_si){}
					if (m_si < m_se && *m_si == '|')
					{
						++m_si;
						return Lexem( LexemListItem1);
					}
				}
				else
				{
					if (start < m_si-1)
					{
						--m_si;
						return Lexem( LexemText, std::string( start, m_si - start));
					}
					return Lexem( LexemEndOfLine);
				}
			}
		}
		else if (*m_si == '}' && m_si+1 < m_se && m_si[1] == '}')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			m_si += 2;
			return Lexem( LexemCloseCitation);
		}
		else if (*m_si == ']')
		{
			if (start != m_si)
			{
				return Lexem( LexemText, std::string( start, m_si - start));
			}
			++m_si;
			if (m_si < m_se && *m_si == ']')
			{
				++m_si;
				return Lexem( LexemCloseLink);
			}
			else
			{
				return Lexem( LexemCloseSquareBracket);
			}
		}
		else
		{
			++m_si;
		}
	}
	if (start != m_si)
	{
		return Lexem( LexemText, std::string( start, m_si - start));
	}
	else
	{
		return Lexem( LexemEOF);
	}
}

enum StateId
{
	StateText,
	StateListItem,
	StateCitation,
	StateLink,
	StateWWWLink,
	StateTable
};
enum TableStateId
{
	TableStateOpen,
	TableStateRow,
	TableStateCol
};
enum TextStateId
{
	TextContent,
	TextSquareBrackets
};

static const char* stateIdName( StateId i)
{
	static const char* ar[] = {"Text","ListItem","Citation","Link","WWWLink","Table"};
	return ar[i];
}
struct State
{
	State( StateId id_)
		:id(id_)
	{
		substate.table = TableStateOpen;
	}
	State( const State& o)
		:id(o.id),substate(o.substate){}
	StateId id;
	union 
	{
		TableStateId table;
		TextStateId text;
	} substate;
};

#ifdef STRUS_LOWLEVEL_DEBUG
static void printStack( std::ostream& out, const std::vector<State>& stack)
{
	std::vector<State>::const_iterator si = stack.begin(), se = stack.end();
	for (int sidx=0; si != se; ++sidx,++si)
	{
		if (sidx) out << " ";
		out << stateIdName(si->id);
	}
}
#endif


static void processDataRowCol( const char* tag, std::string& res, const std::string& data, XmlPrinter& xmlprinter)
{
	Lexer lexer( data.c_str(), data.size());
	std::string id = makeAttributeName( lexer.tryParseIdentifier('='));
	if (id.size())
	{
		std::string rest( trim( lexer.rest()));
		if (xmlprinter.state() == XmlPrinter::TagElement && isAttributeString(rest))
		{
			xmlprinter.printAttribute( id, res);
			xmlprinter.printValue( rest, res);
		}
		else
		{
			xmlprinter.printOpenTag( id, res);
			xmlprinter.printValue( rest, res);
			xmlprinter.printCloseTag( res);
		}
	}
	else if (!isEmpty(data))
	{
		if (tag && tag[0])
		{
			if (tag[0] == '@')
			{
				if (xmlprinter.state() == XmlPrinter::TagElement)
				{
					xmlprinter.printAttribute( tag+1, res);
					xmlprinter.printValue( data, res);
				}
				else
				{
					xmlprinter.printOpenTag( tag+1, res);
					xmlprinter.printValue( data, res);
					xmlprinter.printCloseTag( res);
				}
			}
			else
			{
				xmlprinter.printOpenTag( tag, res);
				xmlprinter.printValue( data, res);
				xmlprinter.printCloseTag( res);
			}
		}
		else
		{
			xmlprinter.printValue( "", res);
			xmlprinter.printValue( data, res);
		}
	}
}

static void processOpenLink( std::string& res, const std::string& tagnam, Lexer& lexer, Lexer::Lexem& lexem, XmlPrinter& xmlprinter)
{
	xmlprinter.printOpenTag( "link", res);
	xmlprinter.printAttribute( "type", res);
	if (tagnam.size())
	{
		xmlprinter.printValue( tagnam, res);
	}
	else
	{
		xmlprinter.printValue( "page", res);
	}
	std::string databuf;
	int lidx = 0;
	for (lexem = lexer.next(); lexem.id == Lexer::LexemText; ++lidx,lexem = lexer.next())
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "LEXEM " << Lexer::lexemIdName( lexem.id) << " '" << outputString( lexem.value.c_str(), lexem.value.c_str() + lexem.value.size()) << "'" << std::endl;
#endif
		if (lidx)
		{
			databuf.push_back( ' ');
		}
		databuf.append( lexem.value);
	}
	processDataRowCol( "@id", res, databuf, xmlprinter);
	if (lexem.id == Lexer::LexemCloseLink)
	{
		processDataRowCol( "", res, databuf, xmlprinter);
	}
	else if (lexem.id == Lexer::LexemColDelim)
	{
		lexem = lexer.next();
	}
}

static void processOpenCitation( std::string& res, const std::string& tagnam, Lexer& lexer, XmlPrinter& xmlprinter)
{
	xmlprinter.printOpenTag( "cit", res);
	if (tagnam.size())
	{
		xmlprinter.printAttribute( "type", res);
		xmlprinter.printValue( tagnam, res);
	}
}

static void processOpenWWWLink( std::string& res, const std::string& tagnam, Lexer& lexer, XmlPrinter& xmlprinter)
{
	xmlprinter.printOpenTag( "wwwlink", res);
	if (tagnam.size())
	{
		xmlprinter.printAttribute( "id", res);
		xmlprinter.printValue( tagnam, res);
	}
}

static void leaveTableState( TableStateId& stateid, std::string& res, XmlPrinter& xmlprinter)
{
	switch (stateid)
	{
		case TableStateCol:
			xmlprinter.printCloseTag( res);
			/*no break here!*/
		case TableStateRow:
			xmlprinter.printCloseTag( res);
			stateid = TableStateOpen;
			/*no break here!*/
		case TableStateOpen:
			break;
	}
}

static void leaveState( State& state, std::string& res, XmlPrinter& xmlprinter)
{
	if (state.id == StateTable)
	{
		leaveTableState( state.substate.table, res, xmlprinter);
	}
}

static std::string processWikimediaText( const char* src, std::size_t size, XmlPrinter& xmlprinter)
{
	bool done = false;
	std::vector<State> stack;
	stack.push_back( StateText);
	std::string databuf;
	static const char* headingTagName[] = {"h1","h2","h3","h4","h5","h6"};
	static const char* listitemTagName[] = {"l1","l2","l3","l4"};

	std::string rt;
	Lexer lexer(src,size);

	for (Lexer::Lexem lexem = lexer.next(); !done; lexem = lexer.next())
	{
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "LEXEM " << Lexer::lexemIdName( lexem.id) << " '" << outputString( lexem.value.c_str(), lexem.value.c_str() + lexem.value.size()) << "'" << std::endl;
#endif
AGAIN:
#ifdef STRUS_LOWLEVEL_DEBUG
		std::cerr << "STACK ";
		printStack( std::cerr, stack);
		std::cerr << std::endl;
#endif
		switch (stack.back().id)
		{
			case StateText:/*no break here!*/
			case StateListItem:
				switch (lexem.id)
				{
					case Lexer::LexemEOF:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						done = true;
						break;
					case Lexer::LexemText:
						xmlprinter.printValue( "", rt);
						xmlprinter.printValue( lexem.value, rt);
						break;
					case Lexer::LexemRedirect:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printOpenTag( "redirect", rt);
						xmlprinter.printValue( "", rt);
						stack.push_back( StateListItem);
						break;
					case Lexer::LexemListItem1:
					case Lexer::LexemListItem2:
					case Lexer::LexemListItem3:
					case Lexer::LexemListItem4:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printOpenTag( listitemTagName[lexem.id-Lexer::LexemListItem1], rt);
						xmlprinter.printValue( "", rt);
						stack.push_back( StateListItem);
						break;
					case Lexer::LexemEndOfLine:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printValue( "\n", rt);
						break;
					case Lexer::LexemHeading1:
					case Lexer::LexemHeading2:
					case Lexer::LexemHeading3:
					case Lexer::LexemHeading4:
					case Lexer::LexemHeading5:
					case Lexer::LexemHeading6:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printOpenTag( headingTagName[lexem.id-Lexer::LexemHeading1], rt);
						xmlprinter.printValue( lexem.value, rt);
						xmlprinter.printCloseTag( rt);
						break;
					case Lexer::LexemOpenCitation:
						processOpenCitation( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateCitation);
						break;
					case Lexer::LexemCloseCitation:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printValue( "\n", rt);
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemOpenWWWLink:
						processOpenWWWLink( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateWWWLink);
						break;
					case Lexer::LexemOpenSquareBracket:
						if (stack.back().substate.text == TextContent)
						{
							xmlprinter.printValue( " [", rt);
							stack.back().substate.text = TextSquareBrackets;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						}
						break;
					case Lexer::LexemCloseSquareBracket:
						if (stack.back().substate.text == TextSquareBrackets)
						{
							xmlprinter.printValue( "] ", rt);
							stack.back().substate.text = TextContent;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						}
						break;
					case Lexer::LexemOpenLink:
						processOpenLink( rt, lexem.value, lexer, lexem, xmlprinter);
						stack.push_back( StateLink);
						goto AGAIN;
					case Lexer::LexemCloseLink:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printValue( "\n", rt);
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemOpenTable:
						xmlprinter.printOpenTag( "table", rt);
						stack.push_back( StateTable);
						break;
					case Lexer::LexemCloseTable:/*no break here!*/
					case Lexer::LexemTableRowDelim:/*no break here!*/
					case Lexer::LexemTableTitle:/*no break here!*/
					case Lexer::LexemTableHeadDelim:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemColDelim:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printValue( "\n", rt);
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						}
						break;
				}
				break;
			case StateCitation:
				if (lexem.id != Lexer::LexemEndOfLine
					&& lexem.id != Lexer::LexemText && !databuf.empty())
				{
					processDataRowCol( "", rt, databuf, xmlprinter);
					databuf.clear();
				}
				switch (lexem.id)
				{
					case Lexer::LexemEOF:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						done = true;
						break;
					case Lexer::LexemText:
						databuf.append( lexem.value);
						break;
					case Lexer::LexemRedirect:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemEndOfLine:
						databuf.append( "\n");
						break;
					case Lexer::LexemListItem1:
					case Lexer::LexemListItem2:
					case Lexer::LexemListItem3:
					case Lexer::LexemListItem4:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printOpenTag( listitemTagName[lexem.id-Lexer::LexemListItem1], rt);
						xmlprinter.printValue( "", rt);
						stack.push_back( StateListItem);
						break;
					case Lexer::LexemHeading1:
					case Lexer::LexemHeading2:
					case Lexer::LexemHeading3:
					case Lexer::LexemHeading4:
					case Lexer::LexemHeading5:
					case Lexer::LexemHeading6:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenCitation:
						processOpenCitation( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateCitation);
						break;
					case Lexer::LexemCloseCitation:
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						break;
					case Lexer::LexemOpenWWWLink:
						processOpenWWWLink( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateWWWLink);
						break;
					case Lexer::LexemOpenSquareBracket:
						if (stack.back().substate.text == TextContent)
						{
							xmlprinter.printValue( "", rt);
							xmlprinter.printValue( " [", rt);
							stack.back().substate.text = TextSquareBrackets;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						}
						break;
					case Lexer::LexemCloseSquareBracket:
						if (stack.back().substate.text == TextSquareBrackets)
						{
							xmlprinter.printValue( "", rt);
							xmlprinter.printValue( "] ", rt);
							stack.back().substate.text = TextContent;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						}
						break;
					case Lexer::LexemOpenLink:
						processOpenLink( rt, lexem.value, lexer, lexem, xmlprinter);
						stack.push_back( StateLink);
						goto AGAIN;
					case Lexer::LexemCloseLink:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenTable:
						xmlprinter.printOpenTag( "table", rt);
						stack.push_back( StateTable);
						break;
					case Lexer::LexemCloseTable:
						if (lexer.eatFollowChar( '}'))
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							break;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
					case Lexer::LexemTableRowDelim:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemTableTitle:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemTableHeadDelim:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemColDelim:
						if (databuf.size())
						{
							databuf.append( ", ");
						}
						break;
				}
				break;
			case StateLink:
				if (lexem.id != Lexer::LexemEndOfLine
					&& lexem.id != Lexer::LexemText && !databuf.empty())
				{
					processDataRowCol( "", rt, databuf, xmlprinter);
					databuf.clear();
				}
				switch (lexem.id)
				{
					case Lexer::LexemEOF:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						done = true;
						break;
					case Lexer::LexemText:
						databuf.append( lexem.value);
						break;
					case Lexer::LexemRedirect:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemEndOfLine:
						databuf.append( "\n");
						break;
					case Lexer::LexemListItem1:
					case Lexer::LexemListItem2:
					case Lexer::LexemListItem3:
					case Lexer::LexemListItem4:
					case Lexer::LexemHeading1:
					case Lexer::LexemHeading2:
					case Lexer::LexemHeading3:
					case Lexer::LexemHeading4:
					case Lexer::LexemHeading5:
					case Lexer::LexemHeading6:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenCitation:
						processOpenCitation( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateCitation);
						break;
					case Lexer::LexemCloseCitation:
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenWWWLink:
						processOpenWWWLink( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateWWWLink);
						break;
					case Lexer::LexemOpenSquareBracket:
						if (stack.back().substate.text == TextContent)
						{
							xmlprinter.printValue( " [", rt);
							stack.back().substate.text = TextSquareBrackets;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						break;
					case Lexer::LexemCloseSquareBracket:
						if (stack.back().substate.text == TextSquareBrackets)
						{
							xmlprinter.printValue( "] ", rt);
							stack.back().substate.text = TextContent;
						}
						else
						{
							OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						break;
					case Lexer::LexemOpenLink:
						processOpenLink( rt, lexem.value, lexer, lexem, xmlprinter);
						stack.push_back( StateLink);
						goto AGAIN;
					case Lexer::LexemCloseLink:
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						break;
					case Lexer::LexemOpenTable:
						xmlprinter.printOpenTag( "table", rt);
						stack.push_back( StateTable);
						break;
					case Lexer::LexemTableRowDelim:/*no break here!*/
					case Lexer::LexemTableTitle:/*no break here!*/
					case Lexer::LexemCloseTable:/*no break here!*/
					case Lexer::LexemTableHeadDelim:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemColDelim:
						if (databuf.size())
						{
							databuf.append( ", ");
						}
						break;
				}
				break;
			case StateWWWLink:
				if (lexem.id != Lexer::LexemEndOfLine
					&& lexem.id != Lexer::LexemText && !databuf.empty())
				{
					processDataRowCol( "", rt, databuf, xmlprinter);
					databuf.clear();
				}
				switch (lexem.id)
				{
					case Lexer::LexemEOF:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						done = true;
						break;
					case Lexer::LexemText:
						databuf.append( lexem.value);
						break;
					case Lexer::LexemRedirect:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemEndOfLine:
						databuf.append( "\n");
						break;
					case Lexer::LexemListItem1:
					case Lexer::LexemListItem2:
					case Lexer::LexemListItem3:
					case Lexer::LexemListItem4:
					case Lexer::LexemHeading1:
					case Lexer::LexemHeading2:
					case Lexer::LexemHeading3:
					case Lexer::LexemHeading4:
					case Lexer::LexemHeading5:
					case Lexer::LexemHeading6:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenCitation:
						processOpenCitation( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateCitation);
						break;
					case Lexer::LexemCloseCitation:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenWWWLink:
						OUT_WARNING << "nested links, unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenSquareBracket:
						OUT_WARNING << "nested links, unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemCloseSquareBracket:
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						break;
					case Lexer::LexemOpenLink:
						processOpenLink( rt, lexem.value, lexer, lexem, xmlprinter);
						stack.push_back( StateLink);
						goto AGAIN;
					case Lexer::LexemCloseLink:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenTable:
						xmlprinter.printOpenTag( "table", rt);
						stack.push_back( StateTable);
						break;
					case Lexer::LexemCloseTable:/*no break here!*/
					case Lexer::LexemTableRowDelim:/*no break here!*/
					case Lexer::LexemTableTitle:/*no break here!*/
					case Lexer::LexemTableHeadDelim:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
						break;
					case Lexer::LexemColDelim:
						if (databuf.size())
						{
							databuf.append( ", ");
						}
						break;
				}
				break;
			case StateTable:
				if (lexem.id != Lexer::LexemEndOfLine
					&& lexem.id != Lexer::LexemText && !databuf.empty())
				{
					processDataRowCol( "i", rt, databuf, xmlprinter);
					databuf.clear();
				}
				switch (lexem.id)
				{
					case Lexer::LexemEOF:
						leaveTableState( stack.back().substate.table, rt, xmlprinter);
						xmlprinter.printCloseTag( rt);
						stack.pop_back();

						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						done = true;
						break;
					case Lexer::LexemText:
						databuf.append( lexem.value);
						break;
					case Lexer::LexemRedirect:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemEndOfLine:
						databuf.append( "\n");
						break;
					case Lexer::LexemListItem1:
					case Lexer::LexemListItem2:
					case Lexer::LexemListItem3:
					case Lexer::LexemListItem4:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printOpenTag( listitemTagName[lexem.id-Lexer::LexemListItem1], rt);
						xmlprinter.printValue( "", rt);
						stack.push_back( StateListItem);
						break;
					case Lexer::LexemHeading1:
					case Lexer::LexemHeading2:
					case Lexer::LexemHeading3:
					case Lexer::LexemHeading4:
					case Lexer::LexemHeading5:
					case Lexer::LexemHeading6:
						OUT_WARNING << "table not closed, unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						leaveTableState( stack.back().substate.table, rt, xmlprinter);
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						goto AGAIN;
					case Lexer::LexemOpenCitation:
						processOpenCitation( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateCitation);
						break;
					case Lexer::LexemCloseCitation:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemOpenWWWLink:
						processOpenWWWLink( rt, lexem.value, lexer, xmlprinter);
						stack.push_back( StateWWWLink);
						break;
					case Lexer::LexemOpenSquareBracket:/*no break here!*/
					case Lexer::LexemCloseSquareBracket:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemOpenLink:
						processOpenLink( rt, lexem.value, lexer, lexem, xmlprinter);
						stack.push_back( StateLink);
						goto AGAIN;
					case Lexer::LexemCloseLink:
						OUT_WARNING << "unexpected lexem " << Lexer::lexemIdName(lexem.id) << " in " << stateIdName(stack.back().id) << " at: " << lexer.currentSourceExtract() << std::endl;
						break;
					case Lexer::LexemOpenTable:
						xmlprinter.printOpenTag( "table", rt);
						stack.push_back( StateTable);
						break;
					case Lexer::LexemCloseTable:
						leaveTableState( stack.back().substate.table, rt, xmlprinter);
						xmlprinter.printCloseTag( rt);
						stack.pop_back();
						break;
					case Lexer::LexemTableRowDelim:
						leaveTableState( stack.back().substate.table, rt, xmlprinter);
						switch (stack.back().substate.table)
						{
							case TableStateCol:
								xmlprinter.printCloseTag( rt);
								stack.back().substate.table = TableStateRow;
								/*no break here!*/
							case TableStateRow:
								xmlprinter.printCloseTag( rt);
								xmlprinter.printOpenTag( "tr", rt);
								break;
							case TableStateOpen:
								xmlprinter.printOpenTag( "tr", rt);
								stack.back().substate.table = TableStateRow;
								break;
						}
						break;
					case Lexer::LexemTableTitle:
						if (stack.back().id == StateListItem)
						{
							xmlprinter.printCloseTag( rt);
							stack.pop_back();
							goto AGAIN;
						}
						xmlprinter.printOpenTag( "title", rt);
						xmlprinter.printValue( "", 0, rt);
						stack.push_back( StateListItem);
						break;
					case Lexer::LexemTableHeadDelim:
						switch (stack.back().substate.table)
						{
							case TableStateCol:
								xmlprinter.printCloseTag( rt);
								xmlprinter.printOpenTag( "th", rt);
								break;
							case TableStateRow:
								xmlprinter.printCloseTag( rt);
								xmlprinter.printOpenTag( "tr", rt);
								xmlprinter.printOpenTag( "th", rt);
								stack.back().substate.table = TableStateCol;
								break;
							case TableStateOpen:
								xmlprinter.printOpenTag( "tr", rt);
								xmlprinter.printOpenTag( "th", rt);
								stack.back().substate.table = TableStateCol;
								break;
						}
						break;
					case Lexer::LexemColDelim:
						switch (stack.back().substate.table)
						{
							case TableStateCol:
								xmlprinter.printCloseTag( rt);
								xmlprinter.printOpenTag( "td", rt);
								break;
							case TableStateRow:
								xmlprinter.printCloseTag( rt);
								xmlprinter.printOpenTag( "tr", rt);
								xmlprinter.printOpenTag( "td", rt);
								stack.back().substate.table = TableStateCol;
								break;
							case TableStateOpen:
								xmlprinter.printOpenTag( "tr", rt);
								xmlprinter.printOpenTag( "td", rt);
								stack.back().substate.table = TableStateCol;
								break;
						}
						break;
				}
				break;
		}
	}
	if (stack.size() > 1)
	{
		OUT_WARNING << "unexpected end of data, not all tags closed" << std::endl;
		while (stack.size() > 1)
		{
			leaveState( stack.back(), rt, xmlprinter);
			stack.pop_back();
			xmlprinter.printCloseTag( rt);
		}
	}
	return rt;
}


static void processText( std::ostream& out, char const* si, std::size_t size)
{
	XmlPrinter xmlprinter( true);
	out << processWikimediaText( si, size, xmlprinter);
}

static textwolf::PositionIndex parseRangeNumber( char const*& ci)
{
	if (*ci < '0' || *ci > '9') throw std::runtime_error("number expected in range definition (option -r)");
	for (; *ci == '0'; ++ci){}
	textwolf::PositionIndex rt = 0;
	for (; *ci >= '0' && *ci <= '9'; ++ci)
	{
		rt = rt * 10 + (*ci -'0');
	}
	if ((*ci|32) == 'k')
	{
		rt *= 1000;
		++ci;
	}
	else if ((*ci|32) == 'm')
	{
		rt *= 1000 * 1000;
		++ci;
	}
	else if ((*ci|32) == 'g')
	{
		rt *= 1000 * 1000 * 1000;
		++ci;
	}
	return rt;
}

class FileOutput
{
public:
	FileOutput( const std::string& filenamefmt_, std::size_t maxprintsize_, const std::string& header_, const std::string& tail_)
		:m_filenamefmt(filenamefmt_),m_header(header_),m_tail(tail_)
		,m_curprintsize(0),m_maxprintsize(maxprintsize_),m_filecnt(0),m_outfile(0)
	{
		openNextFile();
	}

	~FileOutput()
	{
		if (m_outfile) ::fclose( m_outfile);
	}

	void print( const std::string& buf)
	{
		m_curprintsize += buf.size();
		if (!m_outfile) throw std::runtime_error("failed to write to file closed");
		std::size_t written = ::fwrite( buf.c_str(), 1, buf.size(), m_outfile);
		if (written != buf.size())
		{
			throw std::runtime_error( std::string("failed to write to output file: ") + ::strerror( ::ferror(m_outfile)));
		}
	}

	bool synchronize()
	{
		if (m_curprintsize > m_maxprintsize)
		{
			openNextFile();
			return true;
		}
		return false;
	}

	void close()
	{
		if (m_outfile)
		{
			print( m_tail);
			::fclose( m_outfile);
			m_outfile = 0;
		}
	}

private:
	void openNextFile()
	{
		close();
		char filenamebuf[ 256];
		::snprintf( filenamebuf, sizeof(filenamebuf), m_filenamefmt.c_str(), ++m_filecnt);
		m_outfile = ::fopen( filenamebuf, "wb");
		if (!m_outfile)
		{
			char msgbuf[ 512];
			::snprintf( msgbuf, sizeof(msgbuf), "failed to open file %s for writing (%u)", filenamebuf, errno);
			throw std::runtime_error( msgbuf);
		}
		print( m_header);
		m_curprintsize = m_header.size();
	}

private:
	std::string m_filenamefmt;
	std::string m_header;
	std::string m_tail;
	std::size_t m_curprintsize;
	std::size_t m_maxprintsize;
	unsigned int m_filecnt;
	FILE* m_outfile;
};

static int toInteger( const char* val, int minval, int maxval, const char* exceptionstr)
{
	int rt = 0;
	bool sign = false;
	char const* vi = val;
	if (*vi == '-')
	{
		sign = true;
		++vi;
		if (!*vi) throw std::runtime_error( exceptionstr);
	}
	for(; *vi; ++vi)
	{
		if (*vi >= '0' && *vi <= '9')
		{
			rt = rt * 10 + (*vi - '0');
		}
		else
		{
			throw std::runtime_error( exceptionstr);
		}
	}
	if (sign) rt = -rt;
	if (rt < minval || rt > maxval)
	{
		throw std::runtime_error( exceptionstr);
	}
	return rt;
}

int main( int argc, const char* argv[])
{
	int rt = 0;
	try
	{
		strus::local_ptr<FileOutput> fileOutput;
		textwolf::PositionIndex rangeStart = 0;
		textwolf::PositionIndex rangeEnd = std::numeric_limits<textwolf::PositionIndex>::max();
		int minarg = 1;
		std::set<int> namespacemap;
		bool namespaceset = false;
		bool printusage = false;
		while (argc > minarg)
		{
			if (0==std::strcmp(argv[minarg],"-s"))
			{
				g_silent = true;
				++minarg;
			}
			else if (0==std::strcmp(argv[minarg],"-h"))
			{
				printusage = true;
				++minarg;
			}
			else if (0==std::memcmp(argv[minarg],"-r",2))
			{
				std::string rangestring;
				if (argv[minarg][2])
				{
					rangestring.append( argv[minarg]+2);
				}
				else
				{
					++minarg;
					if (argc == minarg)
					{
						throw std::runtime_error( "no argument given for option -r (range)");
					}
					rangestring.append( argv[minarg]);
				}
				char const* ci = rangestring.c_str();
				rangeStart = parseRangeNumber( ci);
				if (*ci == ':' || *ci == ',')
				{
					++ci;
					rangeEnd = parseRangeNumber( ci);
					if (rangeEnd < rangeStart) throw std::runtime_error( "start value of range bigger than end value (option -r)");
					if (*ci) throw std::runtime_error( "unexpected character at end of range definition (option -r)");
				}
				++minarg;
			}
			else if (0==std::memcmp(argv[minarg],"-n",2))
			{
				namespaceset = true;
				int nsidx;
				if (argv[minarg][2])
				{
					nsidx = toInteger( argv[minarg]+2, -2, 3000, "integer between -2 and 3000 expected for option -n");
				}
				else
				{
					++minarg;
					if (argc == minarg)
					{
						throw std::runtime_error( "no argument given for option -n (namespace)");
					}
					nsidx = toInteger( argv[minarg], -2, 3000, "integer between -2 and 3000 expected for option -n");
				}
				namespacemap.insert( nsidx);
				++minarg;
			}
			else if (0==std::memcmp(argv[minarg],"-f",2))
			{
				std::string filenamefmt;
				std::size_t maxprintsize = 1024*1024*1024;
				std::string header;
				std::string tail;
				
				std::string fileoutputstring;
				if (argv[minarg][2])
				{
					fileoutputstring.append( argv[minarg]+2);
				}
				else
				{
					++minarg;
					if (argc == minarg)
					{
						throw std::runtime_error( "no argument given for option -f (file output)");
					}
					fileoutputstring.append( argv[minarg]);
				}
				char const* ci = fileoutputstring.c_str();
				char const* sizeptr = std::strchr( ci, ',');
				if (sizeptr)
				{
					fileoutputstring[ sizeptr-ci] = '\0';
					++sizeptr;
					maxprintsize = parseRangeNumber( sizeptr);
					if (std::strchr( fileoutputstring.c_str(), '%') == 0)
					{
						fileoutputstring.append( "%u");
					}
					if (*sizeptr)
					{
						throw std::runtime_error( "illegal chunk size spezifier after format string in option -f (file output)");
					}
				}
				++minarg;
				XmlPrinter xmlprinter;
				xmlprinter.printHeader( 0, 0, header);
				xmlprinter.printOpenTag( "mediawiki", header);
				xmlprinter.exitTagContext( header);
				xmlprinter.printCloseTag( tail);
				fileOutput.reset( new FileOutput( fileoutputstring, maxprintsize, header, tail));
			}
			else if (argv[minarg][0] == '-' && argv[minarg][1])
			{
				std::cerr << "unknown option '" << argv[minarg] << "'" << std::endl;
				printusage = true;
				++minarg;
			}
			else
			{
				break;
			}
		}
		if (argc == 1 || (argc > minarg+1 || argc <= minarg))
		{
			if (argc > minarg+1) std::cerr << "too many arguments" << std::endl;
			if (argc <= minarg) std::cerr << "too few arguments" << std::endl;
			printusage = true;
		}
		if (printusage)
		{
			std::cerr << "Usage: strusWikimediaToXml [options] <inputfile>" << std::endl;
			std::cerr << "<inputfile>   :File to process or '-' for stdin" << std::endl;
			std::cerr << "options:" << std::endl;
			std::cerr << "    -h         :print this usage" << std::endl;
			std::cerr << "    -s         :silent mode (suppress warnings)" << std::endl;
			std::cerr << "    -r <range> :define range <from>,<to> of bytes" << std::endl;
			std::cerr << "                to output, e.g. 1G,3G for the documents" << std::endl;
			std::cerr << "                starting after the 1st gigabyte to the " << std::endl;
			std::cerr << "                first document ending after the 3rd gigabyte." << std::endl;
			std::cerr << "    -n <ns>    :reduce output to namespace <ns> (0=article)" << std::endl;
			std::cerr << "    -f <format>:split output to files" << std::endl;
			std::cerr << "                  <format> => <filename format string>,<chunk size>" << std::endl;
			return 0;
		}
		strus::InputStream input( argv[minarg]);
		textwolf::StdInputStream inputreader( input.stream());
		textwolf::IStreamIterator inputiterator( &inputreader);
		bool inText = false;
		bool skipDoc = false;
		bool terminated = false;
		int taglevel = 0;

		XmlScanner xs( inputiterator);
		XmlScanner::iterator itr=xs.begin(),end=xs.end();
		XmlPrinter xmlprinter;
		std::string buf;
		std::size_t pagestart = 0;

		xmlprinter.printHeader( 0, 0, buf);
		if (fileOutput.get())
		{
			buf.clear();
		}
		for (; itr!=end && !terminated; ++itr)
		{
#ifdef STRUS_LOWLEVEL_DEBUG
			std::cerr << "element " << itr->name() << " '" << std::string( itr->content(), itr->size()) << "'" << std::endl;
#endif
			if (skipDoc)
			{
				if (itr->type() == XmlScanner::OpenTag)
				{
					++taglevel;
				}
				else if (itr->type() == XmlScanner::CloseTag
					|| itr->type() == XmlScanner::CloseTagIm)
				{
					--taglevel;
					if (taglevel == 1)
					{
						skipDoc = false;
					}
				}
			}
			else switch (itr->type())
			{
				case XmlScanner::None: break;
				case XmlScanner::ErrorOccurred: throw std::runtime_error( itr->content());
				case XmlScanner::HeaderStart:/*no break!*/
				case XmlScanner::HeaderAttribName:/*no break!*/
				case XmlScanner::HeaderAttribValue:/*no break!*/
				case XmlScanner::HeaderEnd:/*no break!*/
				case XmlScanner::DocAttribValue:/*no break!*/
				case XmlScanner::DocAttribEnd:/*no break!*/
				{
					OUT_WARNING << "unexpected element '" << itr->name() << "'" << std::endl;
				}
				case XmlScanner::TagAttribName:
				{
					if (taglevel >= 2 || !fileOutput.get())
					{
						xmlprinter.printAttribute( std::string(itr->content(), itr->size()), buf);
					}
					break;
				}
				case XmlScanner::TagAttribValue:
				{
					if (taglevel >= 2 || !fileOutput.get())
					{
						xmlprinter.printValue( std::string(itr->content(), itr->size()), buf);
					}
					break;
				}
				case XmlScanner::OpenTag: 
				{
					++taglevel;
					if (namespaceset && taglevel == 3 && itr->size() == 2  && 0==std::memcmp( itr->content(), "ns", itr->size()))
					{
						xmlprinter.printOpenTag( std::string(itr->content(), itr->size()), buf);
						++itr;
						if (itr == end)
						{
							terminated = true;
							break;
						}
						if (itr->type() == XmlScanner::Content)
						{
							std::string val( itr->content(), itr->size());
							xmlprinter.printValue( val, buf);
							try
							{
								int nsidx = toInteger( val.c_str(), -2, 3000, "integer between -2 and 3000 expected for tag value of ns");
								if (namespacemap.find( nsidx) == namespacemap.end())
								{
									buf.resize( pagestart);
									skipDoc = true;
								}
							}
							catch (const std::exception& err)
							{
								std::cerr << "error mapping document namespace: " << err.what() << std::endl;
							}
						}
						break;
					}
					if (taglevel == 2 && itr->size() == 4 && 0==std::memcmp( itr->content(), "page", itr->size()))
					{
						pagestart = buf.size();
						textwolf::PositionIndex pos = xs.getIterator().position();
						if (pos < rangeStart)
						{
							skipDoc = true;
							break;
						}
						else if (pos > rangeEnd)
						{
							xmlprinter.printCloseTag( buf);
							terminated = true;
							break;
						}
					}
					if (taglevel >= 2 || !fileOutput.get())
					{
						xmlprinter.printOpenTag( std::string(itr->content(), itr->size()), buf);
					}
					if (itr->size() == 4 && 0==std::memcmp( itr->content(), "text", itr->size()))
					{
						inText = true;
					}
					break;
				}
				case XmlScanner::CloseTagIm:
				case XmlScanner::CloseTag:
				{
					--taglevel;
					inText = false;
					if (taglevel >= 1 || !fileOutput.get())
					{
						xmlprinter.printCloseTag( buf);
					}
					if (taglevel == 1)
					{
						if (fileOutput.get())
						{
							fileOutput->print( buf);
							if (fileOutput->synchronize())
							{
								std::string tmpbuf;
								xmlprinter.printHeader( 0, 0, tmpbuf);
								xmlprinter.printOpenTag( "mediawiki", tmpbuf);
								xmlprinter.exitTagContext( tmpbuf);
							}
						}
						else
						{
							std::cout << buf;
						}
						buf.clear();
						pagestart = 0;
					}
					break;
				}
				case XmlScanner::Content:
				{
					xmlprinter.printValue( "", buf);
					if (inText)
					{
#ifdef STRUS_LOWLEVEL_DEBUG
						std::cerr << std::endl << "PROCESS CONTENT:" << std::endl << std::string( itr->content(), itr->size()) << std::endl << "." << std::endl;
#endif
						std::ostringstream textout;
						processText( textout, itr->content(), itr->size());
						buf.append( textout.str());
					}
					else
					{
						xmlprinter.printValue( std::string(itr->content(), itr->size()), buf);
					}
					break;
				}
				case XmlScanner::Exit: break;
			}
		}
		if (fileOutput.get())
		{
			fileOutput->print( buf);
			fileOutput->close();
		}
		else
		{
			std::cout << buf;
		}
		buf.clear();
		pagestart = 0;
		if (xmlprinter.lasterror())
		{
			throw std::runtime_error( std::string( "textwolf xmlprinter: ") + xmlprinter.lasterror());
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


