#!/usr/bin/python3
import tornado.ioloop
import tornado.web
import tornado.websocket
import tornado.gen
import os
import sys
import struct
import binascii
import collections
import heapq
import optparse
import signal
import strus
import strusMessage
import time
import math
import pprint
import urllib.parse

# [0] Globals and helper classes:
# The address of the global statistics server:
statserver = "localhost:7183"
# The address of the query analyze server:
qryserver = "localhost:7182"
# Strus storage server addresses:
storageservers = []
# Strus client connection factory:
msgclient = strusMessage.RequestClient()
# Globals:
global debugtrace
debugtrace = False

# Query analyzer structures:
strusctx = strus.Context()
strusctx.addResourcePath( "./resources")
analyzer = strusctx.createQueryAnalyzer()
analyzer.addElement(
        "stem", "text", "word", 
        ["lc", ["dictmap", "irregular_verbs_en.txt"], ["stem", "en"], ["convdia", "en"], "lc"]
    )

# Query evaluation structures:
ResultRow = collections.namedtuple('ResultRow', ['serverno','docno', 'weight', 'title', 'paratitle', 'abstract', 'debuginfo'])
NblnkRow = collections.namedtuple('NblnkRow', ['serverno','docno', 'weight', 'links', 'features', 'titles'])
LinkRow = collections.namedtuple('LinkRow', ['title','weight'])
QueryTerm = collections.namedtuple('QueryTerm', ['type','value','length','weight'])
RelatedTerm  = collections.namedtuple('RelatedTerm', ['value', 'encvalue', 'index', 'weight'])
QueryStruct = collections.namedtuple('QueryStruct', ['terms','links','relatedterms','errors'])

def LinkRowKey( linkrow):
    return linkrow.weight

# [1] HTTP handlers:
# Answer a query (issue a query to all storage servers and merge it to one result):
class QueryHandler( tornado.web.RequestHandler ):
    def termStatQuery( self, terms):
        statquery = bytearray(b"Q")
        for term in terms:
            statquery.extend( b'T')
            statquery.extend( strusMessage.packString( term.type))
            statquery.extend( strusMessage.packString( term.value))
        statquery.extend( b'N')
        return statquery

    def linkStatQuery( self, linktype, links):
        statquery = bytearray("Q")
        for link in links:
            statquery.extend( b'T')
            statquery.extend( strusMessage.packString( linktype))
            statquery.extend( strusMessage.packString( link.title))
        statquery.extend( b'N')
        return statquery

    @tornado.gen.coroutine
    def queryStatserver( self, statquery):
        rt = ([],0,None)
        conn = None
        try:
            ri = statserver.rindex(':')
            host,port = statserver[:ri],int( statserver[ri+1:])
            conn = yield msgclient.connect( host, port)
            statreply = yield msgclient.issueRequest( conn, statquery)

            if statreply[0] == ord('E'):
                raise Exception( "failed to query global statistics: %s" % statreply[1:])
            elif statreply[0] != ord('Y'):
                raise Exception( "protocol error loading global statistics")
            dflist = []
            collsize = 0
            statsofs = 1
            statslen = len(statreply)
            while (statsofs < statslen):
                (statsval,) = struct.unpack_from( ">q", statreply, statsofs)
                statsofs += struct.calcsize( ">q")
                dflist.append( statsval)
            collsize = dflist.pop()
            rt = (dflist, collsize, None)
            conn.close()
        except Exception as e:
            errmsg = "query statistic server failed: %s" % e;
            if conn:
                conn.close()
            rt = ([],0,errmsg)
        raise tornado.gen.Return( rt)


    def unpackAnswerTextQuery( self, answer, answerofs, answersize):
        result = []
        serverno = 0
        row_docno = 0
        row_weight = 0.0
        row_title = None
        row_paratitle = None
        row_abstract = None
        row_debuginfo = None
        while (answerofs < answersize):
            if answer[ answerofs] == ord('_'):
                if not row_title is None:
                    result.append( ResultRow( serverno, row_docno, row_weight, row_title, row_paratitle, row_abstract, row_debuginfo))
                row_docno = 0
                row_weight = 0.0
                row_title = None
                row_paratitle = None
                row_abstract = None
                row_debuginfo = None
                answerofs += 1
            elif answer[ answerofs] == ord('D'):
                (row_docno,) = struct.unpack_from( ">I", answer, answerofs+1)
                answerofs += struct.calcsize( ">I") + 1
            elif answer[ answerofs] == ord('W'):
                (row_weight,) = struct.unpack_from( ">d", answer, answerofs+1)
                answerofs += struct.calcsize( ">d") + 1
            elif answer[ answerofs] == ord('T'):
                (row_title,answerofs) = strusMessage.unpackString( answer, answerofs+1)
            elif answer[ answerofs] == ord('P'):
                (row_paratitle,answerofs) = strusMessage.unpackString( answer, answerofs+1)
            elif answer[ answerofs] == ord('A'):
                (row_abstract,answerofs) = strusMessage.unpackString( answer, answerofs+1)
            elif answer[ answerofs] == ord('B'):
                (row_debuginfo,answerofs) = strusMessage.unpackString( answer, answerofs+1)
            elif answer[ answerofs] == ord('Z'):
                (serverno,) = struct.unpack_from( ">H", answer, answerofs+1)
                answerofs += struct.calcsize( ">H") + 1
            else:
                raise Exception( "protocol error: unknown result column name '%c'" % (answer[answerofs]))
        if not row_title is None:
            result.append( ResultRow( serverno, row_docno, row_weight, row_title, row_paratitle, row_abstract, row_debuginfo))
        return result

    def unpackAnswerLinkQuery( self, answer, answerofs, answersize):
        result = []
        serverno = 0
        row_docno = 0
        row_weight = 0.0
        row_links = []
        row_titles = []
        row_features = []
        while (answerofs < answersize):
            if answer[ answerofs] == ord('_'):
                if row_docno != 0:
                    result.append( NblnkRow( serverno, row_docno, row_weight, row_links, row_features, row_titles))
                row_docno = 0
                row_weight = 0.0
                row_links = []
                row_titles = []
                row_features = []
                answerofs += 1
            elif answer[ answerofs] == ord('D'):
                (row_docno,) = struct.unpack_from( ">I", answer, answerofs+1)
                answerofs += struct.calcsize( ">I") + 1
            elif answer[ answerofs] == ord('W'):
                (row_weight,) = struct.unpack_from( ">d", answer, answerofs+1)
                answerofs += struct.calcsize( ">d") + 1
            elif answer[ answerofs] == ord('L'):
                (idstr,answerofs) = strusMessage.unpackString( answer, answerofs+1)
                (weight,) = struct.unpack_from( ">d", answer, answerofs)
                answerofs += struct.calcsize( ">d")
                row_links.append([idstr,weight])
            elif answer[ answerofs] == ord('F'):
                (idstr,answerofs) = strusMessage.unpackString( answer, answerofs+1)
                (weight,) = struct.unpack_from( ">d", answer, answerofs)
                answerofs += struct.calcsize( ">d")
                row_features.append([idstr,weight])
            elif answer[ answerofs] == ord('T'):
                (idstr,answerofs) = strusMessage.unpackString( answer, answerofs+1)
                (weight,) = struct.unpack_from( "d", answer, answerofs)
                answerofs += struct.calcsize( ">d")
                row_titles.append([idstr,weight])
            elif answer[ answerofs] == ord('Z'):
                (serverno,) = struct.unpack_from( ">H", answer, answerofs+1)
                answerofs += struct.calcsize( ">H") + 1
            else:
                raise Exception( "protocol error: unknown result column name '%c'" % (answer[answerofs]))
        if row_docno != 0:
            result.append( NblnkRow( serverno, row_docno, row_weight, row_links, row_features, row_titles))
        return result

    @tornado.gen.coroutine
    def issueQuery( self, serveraddr, scheme, qryblob):
        rt = (None,None)
        ri = serveraddr.rindex(':')
        host,port = serveraddr[:ri],int( serveraddr[ri+1:])
        result = None
        conn = None
        try:
            conn = yield msgclient.connect( host, port)
            reply = yield msgclient.issueRequest( conn, qryblob)
            if reply[0] == ord('E'):
                rt = (None, "storage server %s:%d returned error: %s" % (host, port, reply[1:]))
            elif reply[0] == ord('Y'):
                if scheme == "NBLNK" or scheme == "TILNK" or scheme == "VCLNK" or scheme == "STDLNK":
                    result = self.unpackAnswerLinkQuery( reply, 1, len(reply)-1)
                else:
                    result = self.unpackAnswerTextQuery( reply, 1, len(reply)-1)
                rt = (result, None)
            else:
                rt = (None, "protocol error storage %s:%u query: unknown header %s" % (host,port,reply[0]))
            conn.close()
        except Exception as e:
            if conn:
                conn.close()
            rt = (None, "call of storage server %s:%u failed: %s" % (host, port, e))
        raise tornado.gen.Return( rt)

    @tornado.gen.coroutine
    def issueQueries( self, servers, scheme, qryblob):
        results = None
        try:
            results = yield [ self.issueQuery( addr, scheme, qryblob) for addr in servers ]
        except Exception as e:
            raise tornado.gen.Return( [], ["error issueing query: %s" % e])
        raise tornado.gen.Return( results)

    # Merge code derived from Python Cookbook (Sebastien Keim, Raymond Hettinger and Danny Yoo)
    # referenced in from http://wordaligned.org/articles/merging-sorted-streams-in-python:
    def mergeResultIter( self, resultlists):
        # prepare a priority queue whose items are pairs of the form (-weight, resultlistiter):
        heap = [ ]
        iterar = [ ]
        for resultlist in resultlists:
            resultlistiter = iter(resultlist)
            for result in resultlistiter:
                # subseq is not empty, therefore add this subseq's pair
                # (current-value, iterator) to the list
                heap.append((-result.weight, result, len(iterar)))
                iterar.append( resultlistiter)
                break
        # make the priority queue into a heap
        heapq.heapify(heap)
        while heap:
            # get and yield the result with the highest weight (minus lowest negative weight):
            negative_weight, result, resultlistidx = heap[0]
            yield result
            for result in iterar[ resultlistidx]:
                # resultlists is not finished, replace best pair in the priority queue
                heapq.heapreplace( heap, (-result.weight, result, resultlistidx))
                break
            else:
                # subseq has been exhausted, therefore remove it from the queue
                heapq.heappop( heap)

    def mergeQueryResults( self, results, firstrank, nofranks):
        merged = []
        errors = []
        itrs = []
        maxnofresults = firstrank + nofranks
        for result in results:
            if result[0] is None:
                errors.append( result[1])
            else:
                itrs.append( iter( result[0]))
        ri = 0
        for result in self.mergeResultIter( itrs):
            if ri == maxnofresults:
                break
            merged.append( result)
            ri += 1
        return (merged[ firstrank:maxnofresults], errors)

    def getLinkQueryResults( self, ranks, linkname, firstlink, noflinks):
        results = []
        linktab = {}
        linkreftab = {}
        docidtab = {}
        docreftab = {}
        for rank in ranks:
            for link in getattr( rank, linkname):
                refcnt = 1
                docid = link[0]
                if docid in docreftab:
                    refcnt = 1 + docreftab[ docid]
                    docreftab[ docid] = refcnt
                else:
                    docreftab[ docid] = 1

                linkid = link[0]
                if linkid in linktab:
                    linktab[ linkid] = 0.0 + linktab[ linkid] + link[1] * rank.weight
                    _refcnt,_docid = linkreftab[ linkid]
                    if _refcnt < refcnt:
                        linkreftab[ linkid] = [refcnt,docid]
                else:
                    linktab[ linkid] = 0.0 + link[1] * rank.weight
                    linkreftab[ linkid] = [refcnt,docid]
        heap = [ ]
        for (link,weight) in linktab.items():
            heap.append([-weight, link])
        heapq.heapify(heap)
        li = 0
        le = firstlink + noflinks
        while li<le and heap:
            negweight,title = heapq.heappop( heap)
            if li >= firstlink:
                _refcnt,_docid = linkreftab[ title]
                results.append( LinkRow( _docid, -negweight))
            li = li + 1
        return results

    @tornado.gen.coroutine
    def analyzeQuery( self, scheme, querystr, nofranks):
        terms = []
        relatedterms = []
        errors = []
        conn = None
        try:
            query = bytearray(b"Q")
            query.extend(b'X')
            query.extend( strusMessage.packString( querystr))
            query.extend( b'N')
            query.extend( struct.pack(">H", nofranks))

            ri = qryserver.rindex(':')
            host,port = qryserver[:ri],int( qryserver[ri+1:])
            conn = yield msgclient.connect( host, port)
            reply = yield msgclient.issueRequest( conn, query)
            if reply[0] == ord('E'):
                raise Exception( "failed to query analyze server: %s" % reply[1:])
            elif reply[0] != ord('Y'):
                raise Exception( "protocol error in query analyze server query")
            replyofs = 1
            replylen = len(reply)
            while replyofs < replylen:
                if reply[ replyofs] == ord('T'):
                    replyofs += 1
                    type = None
                    value = None
                    length = 1
                    while replyofs < replylen:
                        if reply[ replyofs] == ord('T'):
                            (type,replyofs) = strusMessage.unpackString( reply, replyofs+1)
                        elif reply[ replyofs] == ord('V'):
                            (value,replyofs) = strusMessage.unpackString( reply, replyofs+1)
                        elif reply[ replyofs] == ord('L'):
                            (length,) = struct.unpack_from( ">I", reply, replyofs+1)
                            replyofs += struct.calcsize( ">I") + 1
                        elif reply[ replyofs] == ord('_'):
                            replyofs += 1
                            break
                    terms.append( QueryTerm( type, value, length, 1.0) )
                elif reply[ replyofs] == ord('R'):
                    replyofs += 1
                    value = None
                    index = -1
                    weight = 0.0
                    while replyofs < replylen:
                        if reply[ replyofs] == ord('V'):
                            (value,replyofs) = strusMessage.unpackString( reply, replyofs+1)
                        elif reply[ replyofs] == ord('I'):
                            (index,) = struct.unpack_from( ">I", reply, replyofs+1)
                            replyofs += struct.calcsize( ">I") + 1
                        elif reply[ replyofs] == ord('W'):
                            (weight,) = struct.unpack_from( ">d", reply, replyofs+1)
                            replyofs += struct.calcsize( ">d") + 1
                        elif reply[ replyofs] == ord('_'):
                            replyofs += 1
                            break
                    valuestr = value.replace('_',' ')
                    if (valuestr.lower() != querystr.lower()):
                        encvalue = urllib.parse.quote( valuestr)
                        relatedterms.append( RelatedTerm( valuestr, encvalue, index, weight) )
                else:
                    break
            if replyofs != replylen:
                raise Exception("query analyze server result format error")
            conn.close()
        except Exception as e:
            errors.append( "query analyze server request failed: %s" % e);
            if conn:
                conn.close()
            alt_terms = analyzer.analyzeTermExpression( ["text",querystr] )
            for term in alt_terms:
                terms.append( QueryTerm( term.type, term.value, term.length, 1.0))
        raise tornado.gen.Return( QueryStruct( terms, [], relatedterms, errors) )

    @tornado.gen.coroutine
    def evaluateQuery( self, scheme, querystruct, firstrank, nofranks, restrictdn, with_debuginfo):
        rt = None
        try:
            maxnofresults = firstrank + nofranks
            terms = querystruct.terms
            if not terms:
                # Return empty result for empty query:
                rt = [[],[]]
            else:
                # Get the global statistics:
                dflist,collectionsize,error = yield self.queryStatserver( self.termStatQuery( terms))
                if not error is None:
                    raise Exception( error)
                # Assemble the query:
                qry = bytearray( b"Q")
                qry.extend( bytearray( b"M"))
                qry.extend( strusMessage.packString( scheme))
                qry.extend( bytearray( b"S"))
                qry.extend( struct.pack( ">q", collectionsize))
                qry.extend( bytearray( b"I"))
                qry.extend( struct.pack( ">H", 0))
                qry.extend( bytearray( b"N"))
                qry.extend( struct.pack( ">H", maxnofresults))
                if with_debuginfo:
                    qry.extend( bytearray( b"B"))
                if restrictdn != 0:
                    qry.extend( bytearray( b"D"))
                    qry.extend( struct.pack( ">I", restrictdn))
                for ii in range( 0, len( terms)):
                    qry.extend( bytearray( b"T"))
                    qry.extend( strusMessage.packString( terms[ii].type))
                    qry.extend( strusMessage.packString( terms[ii].value))
                    if (terms[ii].length):
                        qry.extend( struct.pack( ">Hqd", terms[ii].length, dflist[ii], 1.0))
                    else:
                        qry.extend( struct.pack( ">Hqd", 1, dflist[ii], 1.0))
                for lnk in querystruct.links:
                    qry.extend( bytearray( b"L"))
                    qry.extend( strusMessage.packString( "vectfeat"))
                    qry.extend( strusMessage.packString( lnk.title))
                    qry.extend( struct.pack( ">d", lnk.weight))
                # Query all storage servers:
                results = yield self.issueQueries( storageservers, scheme, qry)
                rt = self.mergeQueryResults( results, firstrank, nofranks)
        except Exception as e:
            rt = ([], ["error evaluation query: %s" % e])
        raise tornado.gen.Return( rt)

    @tornado.gen.coroutine
    def get(self):
        try:
            # q = query terms:
            querystr = self.get_argument( "q", "")
            # i = firstrank:
            firstrank = int( self.get_argument( "i", 0))
            # n = nofranks:
            nofranks = int( self.get_argument( "n", 6))
            # s = query evaluation scheme:
            scheme = self.get_argument( "s", "STD")
            # d = document number to restrict to:
            restrictdn = int( self.get_argument( "d", 0))
            # m = mode {"debug"}:
            mode = self.get_argument( "m", None)
            # Evaluate query:
            start_time = time.time()
            # Analyze query:
            querystruct = yield self.analyzeQuery( scheme, querystr, 30)
            errors = querystruct.errors
            relatedterms = None
            nblinks = None
            features = None
            with_debuginfo = (mode == "debug")

            if scheme == "NBLNK" or scheme == "TILNK" or scheme == "VCLNK" or scheme == "STDLNK":
                selectresult = yield self.evaluateQuery( scheme, querystruct, 0, 120, restrictdn, with_debuginfo)
                errors.extend( selectresult[1])
                result = [self.getLinkQueryResults( selectresult[0], 'links', firstrank, nofranks+1), errors]
            elif scheme == "STD":
                noflinks = 20
                nofnblinks = 20
                noffeatures = 15
                selectresult = yield self.evaluateQuery( "STDLNK", querystruct, 0, 120, 0, False)
                errors.extend( selectresult[1])
                links = self.getLinkQueryResults( selectresult[0], 'links', 0, noflinks)
                nblinks = self.getLinkQueryResults( selectresult[0], 'titles', 0, nofnblinks)
                features = self.getLinkQueryResults( selectresult[0], 'features', 0, noffeatures)
                if len(links) >= 1:
                    maplinks = []
                    for link in links:
                        maplinks.append( LinkRow( link.title, math.log( link.weight)))
                    links = maplinks
                relatedterms = querystruct.relatedterms
                querystruct = QueryStruct( querystruct.terms, links, [], errors)
                qryresult = yield self.evaluateQuery( "BM25std", querystruct, firstrank, nofranks+1, restrictdn, with_debuginfo)
                errors.extend( qryresult[1])
                result = [qryresult[0],errors]
            else:
                qryresult = yield self.evaluateQuery( scheme, querystruct, firstrank, nofranks+1, restrictdn, with_debuginfo)
                errors.extend( qryresult[1])
                result = [qryresult[0],errors]
            time_elapsed = time.time() - start_time
            # Render the results:
            if scheme == "NBLNK" or scheme == "TILNK" or scheme == "VCLNK" or scheme == "STDLNK":
               template = "search_nblnk_html.tpl"
            else:
               template = "search_documents_html.tpl"
            if len(result[0]) > nofranks:
                hasmore = True
                ranklist = result[0][:-1]
            else:
                hasmore = False
                ranklist = result[0]
            self.render( template,
                         results=ranklist, relatedterms=relatedterms, nblinks=nblinks, features=features, hasmore=hasmore, messages=result[1],
                         time_elapsed=time_elapsed, firstrank=firstrank, nofranks=nofranks, mode=mode,
                         scheme=scheme, querystr=querystr)
        except Exception as e:
            self.render( "search_error_html.tpl", message=e)


# [2] Dispatcher:
application = tornado.web.Application([
    # /query in the URL triggers the handler for answering queries:
    (r"/query", QueryHandler),
    # files like images referenced in tornado templates:
    (r"/static/(.*)",tornado.web.StaticFileHandler,
        {"path": os.path.dirname(os.path.realpath(sys.argv[0]))},)
])

def on_shutdown():
    print('Shutting down')
    tornado.ioloop.IOLoop.current().stop()

# [5] Server main:
if __name__ == "__main__":
    try:
        # Parse arguments:
        usage = "usage: %prog [options] {<storage server port>}"
        parser = optparse.OptionParser( usage=usage)
        parser.add_option("-p", "--port", dest="port", default=80,
                          help="Specify the port of this server as PORT (default %u)" % 80,
                          metavar="PORT")
        parser.add_option("-s", "--statserver", dest="statserver", default=statserver,
                          help="Specify the address of the statistics server as ADDR (default %s" % statserver,
                          metavar="ADDR")
        parser.add_option("-G", "--debug", action="store_true", dest="do_debugtrace", default=False,
                          help="Tell the node to print some messages for tracing what it does")

        (options, args) = parser.parse_args()
        myport = int(options.port)
        statserver = options.statserver
        if statserver[0:].isdigit():
            statserver = '{}:{}'.format( 'localhost', statserver)
        debugtrace = options.do_debugtrace

        # Positional arguments are storage server addresses, if empty use default at localhost:7184
        for arg in args:
            if arg[0:].isdigit():
                storageservers.append( '{}:{}'.format( 'localhost', arg))
            else:
                storageservers.append( arg)
        if len( storageservers) == 0:
            storageservers.append( "localhost:7184")

        # Start server:
        print( "Starting server ...")
        application.listen( myport )
        print( "Listening on port %u" % myport )
        ioloop = tornado.ioloop.IOLoop.current()
        signal.signal( signal.SIGINT, lambda sig, frame: ioloop.add_callback_from_signal(on_shutdown))
        ioloop.start()
        print( "Terminated")
    except Exception as e:
        print( e)


