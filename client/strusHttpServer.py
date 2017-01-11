#!/usr/bin/python
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
import unidecode
import pprint

# [0] Globals and helper classes:
# The address of the global statistics server:
statserver = "localhost:7183"
# The address of the global dym (did you mean) server:
dymserver = "localhost:7189"
# Strus storage server addresses:
storageservers = []
# Strus client connection factory:
msgclient = strusMessage.RequestClient()

# Query analyzer structures:
strusctx = strus.Context()
strusctx.addResourcePath( "./resources")
analyzer = strusctx.createQueryAnalyzer()
analyzer.addSearchIndexElement(
        "stem", "text", "word", 
        ["lc", ["dictmap", "irregular_verbs_en.txt"], ["stem", "en"], ["convdia", "en"], "lc"]
    )

# Query evaluation structures:
ResultRow = collections.namedtuple('ResultRow', ['docno', 'weight', 'title', 'paratitle', 'abstract'])
NblnkRow = collections.namedtuple('NblnkRow', ['docno', 'weight', 'links'])
LinkRow = collections.namedtuple('LinkRow', ['title','weight'])

# [1] HTTP handlers:
# Answer a query (issue a query to all storage servers and merge it to one result):
class QueryHandler( tornado.web.RequestHandler ):
    @tornado.gen.coroutine
    def queryStats( self, terms):
        rt = ([],0,None)
        conn = None
        try:
            statquery = bytearray("Q")
            for term in terms:
                statquery.append('T')
                typesize = len( term.type())
                valuesize = len( term.value())
                statquery += struct.pack( ">HH", typesize, valuesize)
                statquery += struct.pack( "%ds%ds" % (typesize,valuesize), term.type(), term.value())
            statquery.append('N')
            ri = statserver.rindex(':')
            host,port = statserver[:ri],int( statserver[ri+1:])
            conn = yield msgclient.connect( host, port)
            statreply = yield msgclient.issueRequest( conn, statquery)

            if (statreply[0] == 'E'):
                raise Exception( "failed to query global statistics: %s" % statreply[1:])
            elif (statreply[0] != 'Y'):
                raise Exception( "protocol error loading global statistics")
            dflist = []
            collsize = 0
            statsofs = 1
            statslen = len(statreply)
            while (statsofs < statslen):
                (statsval,) = struct.unpack_from( ">q", statreply, statsofs)
                statsofs += struct.calcsize( ">q")
                if (len(dflist) < len(terms)):
                    dflist.append( statsval)
                elif (len(dflist) == len(terms)):
                    collsize = statsval
                else:
                    break
            if (statsofs != statslen):
                raise Exception("result does not match query")
            rt = (dflist, collsize, None)
            conn.close()
        except Exception as e:
            errmsg = "query statistic server failed: %s" % e;
            if (conn):
                conn.close()
            rt = ([],0,errmsg)
        raise tornado.gen.Return( rt)


    def unpackAnswerTextQuery( self, answer, answerofs, answersize):
        result = []
        row_docno = 0
        row_weight = 0.0
        row_title = None
        row_paratitle = ""
        row_abstract = ""
        while (answerofs < answersize):
            if (answer[ answerofs] == '_'):
                if not row_title is None:
                    result.append( ResultRow( row_docno, row_weight, row_title, row_paratitle, row_abstract))
                row_docno = 0
                row_weight = 0.0
                row_title = None
                row_paratitle = ""
                row_abstract = ""
                answerofs += 1
            elif (answer[ answerofs] == 'D'):
                (row_docno,) = struct.unpack_from( ">I", answer, answerofs+1)
                answerofs += struct.calcsize( ">I") + 1
            elif (answer[ answerofs] == 'W'):
                (row_weight,) = struct.unpack_from( ">f", answer, answerofs+1)
                answerofs += struct.calcsize( ">f") + 1
            elif (answer[ answerofs] == 'T'):
                (titlelen,) = struct.unpack_from( ">H", answer, answerofs+1)
                answerofs += struct.calcsize( ">H") + 1
                (row_title,) = struct.unpack_from( "%us" % titlelen, answer, answerofs)
                answerofs += titlelen
            elif (answer[ answerofs] == 'P'):
                (paratitlelen,) = struct.unpack_from( ">H", answer, answerofs+1)
                answerofs += struct.calcsize( ">H") + 1
                (row_paratitle,) = struct.unpack_from( "%us" % paratitlelen, answer, answerofs)
                answerofs += paratitlelen
            elif (answer[ answerofs] == 'A'):
                (abstractlen,) = struct.unpack_from( ">H", answer, answerofs+1)
                answerofs += struct.calcsize( ">H") + 1
                (row_abstract,) = struct.unpack_from( "%us" % abstractlen, answer, answerofs)
                answerofs += abstractlen
            else:
                raise Exception( "protocol error: unknown result column name '%c'" % (answer[answerofs]))
        if not row_title is None:
            result.append( ResultRow( row_docno, row_weight, row_title, row_paratitle, row_abstract))
        return result

    def unpackAnswerLinkQuery( self, answer, answerofs, answersize):
        result = []
        row_docno = 0
        row_weight = 0.0
        row_links = []
        while (answerofs < answersize):
            if (answer[ answerofs] == '_'):
                if (row_docno != 0):
                    result.append( NblnkRow( row_docno, row_weight, row_links))
                row_docno = 0
                row_weight = 0.0
                row_links = []
                answerofs += 1
            elif (answer[ answerofs] == 'D'):
                (row_docno,) = struct.unpack_from( ">I", answer, answerofs+1)
                answerofs += struct.calcsize( ">I") + 1
            elif (answer[ answerofs] == 'W'):
                (row_weight,) = struct.unpack_from( ">f", answer, answerofs+1)
                answerofs += struct.calcsize( ">f") + 1
            elif (answer[ answerofs] == 'L'):
                (linkidlen,) = struct.unpack_from( ">H", answer, answerofs+1)
                answerofs += struct.calcsize( ">H") + 1
                (linkidstr,weight) = struct.unpack_from( ">%dsf" % linkidlen, answer, answerofs)
                answerofs += linkidlen + struct.calcsize( ">f")
                row_links.append([linkidstr,weight])
            else:
                raise Exception( "protocol error: unknown result column name '%c'" % (answer[answerofs]))
        if (row_docno != 0):
            result.append( NblnkRow( row_docno, row_weight, row_links))
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
            if (reply[0] == 'E'):
                rt = (None, "storage server %s:%d returned error: %s" % (host, port, reply[1:]))
            elif (reply[0] == 'Y'):
                if scheme == "NBLNK":
                    result = self.unpackAnswerLinkQuery( reply, 1, len(reply)-1)
                else:
                    result = self.unpackAnswerTextQuery( reply, 1, len(reply)-1)
                rt = (result, None)
            else:
                rt = (None, "protocol error storage %s:%u query: unknown header %s" % (host,port,reply[0]))
            conn.close()
        except Exception as e:
            if (conn):
                conn.close()
            rt = (None, "call of storage server %s:%u failed: %s" % (host, port, str(e)))
        raise tornado.gen.Return( rt)

    @tornado.gen.coroutine
    def issueQueries( self, servers, scheme, qryblob):
        results = None
        try:
            results = yield [ self.issueQuery( addr, scheme, qryblob) for addr in servers ]
        except Exception as e:
            raise tornado.gen.Return( [], ["error issueing query: %s" % str(e)])
        raise tornado.gen.Return( results)

    # Merge code derived from Python Cookbook (Sebastien Keim, Raymond Hettinger and Danny Yoo)
    # referenced in from http://wordaligned.org/articles/merging-sorted-streams-in-python:
    def mergeResultIter( self, resultlists):
        # prepare a priority queue whose items are pairs of the form (-weight, resultlistiter):
        heap = [ ]
        for resultlist in resultlists:
            resultlistiter = iter(resultlist)
            for result in resultlistiter:
                # subseq is not empty, therefore add this subseq's pair
                # (current-value, iterator) to the list
                heap.append((-result.weight, result, resultlistiter))
                break
        # make the priority queue into a heap
        heapq.heapify(heap)
        while heap:
            # get and yield the result with the highest weight (minus lowest negative weight):
            negative_weight, result, resultlistiter = heap[0]
            yield result
            for result in resultlistiter:
                # resultlists is not finished, replace best pair in the priority queue
                heapq.heapreplace( heap, (-result.weight, result, resultlistiter))
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
            if (ri == maxnofresults):
                break
            merged.append( result)
            ri += 1
        return (merged[ firstrank:maxnofresults], errors)

    def getLinkQueryResults( self, ranks, firstlink, noflinks):
        results = []
        linktab = {}
        linkreftab = {}
        docidtab = {}
        docreftab = {}
        for rank in ranks:
            for link in rank.links:
                refcnt = 1
                docid = link[0]
                if docid in docreftab:
                    refcnt = 1 + docreftab[ docid]
                    docreftab[ docid] = refcnt
                else:
                    docreftab[ docid] = 1

                linkid = unidecode.unidecode( link[0].decode('utf-8')).lower()
                if linkid in linktab:
                    linktab[ linkid] = 0.0 + linktab[ linkid] + link[1] * rank.weight
                    _refcnt,_docid = linkreftab[ linkid]
                    if (_refcnt < refcnt):
                        linkreftab[ linkid] = [refcnt,docid]
                else:
                    linktab[ linkid] = 0.0 + link[1] * rank.weight
                    linkreftab[ linkid] = [refcnt,docid]
        heap = [ ]
        for (link,weight) in linktab.iteritems():
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
    def evaluateQueryText( self, scheme, querystr, firstrank, nofranks, restrictdn):
        rt = None
        try:
            maxnofresults = firstrank + nofranks
            terms = analyzer.analyzeField( "text", querystr)
            if len( terms) == 0:
                # Return empty result for empty query:
                rt = [[],[]]
            else:
                # Get the global statistics:
                dflist,collectionsize,error = yield self.queryStats( terms)
                if (not error is None):
                    raise Exception( error)
                # Assemble the query:
                qry = bytearray()
                if scheme == "NBLNK":
                    qry += bytearray( b"L")
                else:
                    qry += bytearray( b"Q")
                qry += bytearray( b"M") + struct.pack( ">H%ds" % (len(scheme)), len(scheme), scheme)
                qry += bytearray( b"S") + struct.pack( ">q", collectionsize)
                qry += bytearray( b"I") + struct.pack( ">H", 0)
                qry += bytearray( b"N") + struct.pack( ">H", maxnofresults)
                if (restrictdn != 0):
                    qry += bytearray( b"D") + struct.pack( ">I", restrictdn)
                for ii in range( 0, len( terms)):
                    qry += bytearray( b"T")
                    typesize = len(terms[ii].type())
                    valuesize = len(terms[ii].value())
                    qry += struct.pack( ">qHH", dflist[ii], typesize, valuesize)
                    qry += struct.pack( "%ds%ds" % (typesize,valuesize), terms[ii].type(), terms[ii].value())
                # Query all storage servers:
                results = yield self.issueQueries( storageservers, scheme, qry)
                rt = self.mergeQueryResults( results, firstrank, nofranks)
        except Exception as e:
            rt = ([], ["error evaluation query: %s" % str(e)])
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
            scheme = self.get_argument( "s", "BM25pff").encode('utf-8')
            # d = document number to restrict to:
            restrictdn = int( self.get_argument( "d", 0))
            # m = mode {"debug"}:
            mode = self.get_argument( "m", None)
            # Evaluate query:
            start_time = time.time()
            if (scheme == "NBLNK"):
                selectresult = yield self.evaluateQueryText( scheme, querystr, 0, 100, restrictdn)
                result = [self.getLinkQueryResults( selectresult[0], firstrank, nofranks), selectresult[1]]
            else:
                result = yield self.evaluateQueryText( scheme, querystr, firstrank, nofranks+1, restrictdn)
            time_elapsed = time.time() - start_time
            # Render the results:
            if (scheme == "NBLNK"):
               template = "search_nblnk_html.tpl"
            else:
               template = "search_bm25_html.tpl"
            self.render( template, results=result[0][:-1], hasmore=(len(result[0])>nofranks), messages=result[1],
                         time_elapsed=time_elapsed, firstrank=firstrank, maxnofranks=nofranks, mode=mode,
                         scheme=scheme, querystr=querystr)
        except Exception as e:
            self.render( "search_error_html.tpl", message=e)

# Answer a DYM (did you mean) query:
class DymHandler( tornado.web.RequestHandler ):
    @tornado.gen.coroutine
    def evaluateDymQuery( self, oldquerystr, querystr, nofranks, nofresults, restrictdnlist):
        rt = ([],None)
        conn = None
        try:
            query = bytearray("Q")
            query.append('S')
            querystrsize = len( querystr)
            query += struct.pack( ">H%ds" % (querystrsize), querystrsize, querystr)
            query.append('N')
            query += struct.pack( ">H", nofranks)
            for restrictdn in restrictdnlist:
                query.append('D')
                query += struct.pack( ">I", restrictdn)

            ri = dymserver.rindex(':')
            host,port = dymserver[:ri],int( dymserver[ri+1:])
            conn = yield msgclient.connect( host, port)
            reply = yield msgclient.issueRequest( conn, query)
            if (reply[0] == 'E'):
                raise Exception( "failed to query dym server: %s" % reply[1:])
            elif (reply[0] != 'Y'):
                raise Exception( "protocol error in dym server query")
            proposals = []
            replyofs = 1
            replylen = len(reply)
            while (replyofs < replylen):
                if (reply[ replyofs] == '_'):
                    replyofs += 1
                if (reply[ replyofs] == 'P'):
                    replyofs += 1
                    (propsize,) = struct.unpack_from( ">H", reply, replyofs)
                    replyofs += struct.calcsize( ">H")
                    (propstr,) = struct.unpack_from( "%ds" % (propsize), reply, replyofs)
                    replyofs += propsize
                    proposals.append( propstr)

            if (replyofs != replylen):
                raise Exception("dym server result format error")
            rt = (proposals, None)
            conn.close()
        except Exception as e:
            errmsg = "dym server request failed: %s" % e;
            if (conn):
                conn.close()
            rt = ([], errmsg)
        raise tornado.gen.Return( rt)

    @tornado.gen.coroutine
    def get(self):
        try:
            # q = query string:
            querystr = self.get_argument( "q", "").encode('utf-8')
            oldquerystr = self.get_argument( "o", "").encode('utf-8')
            # n = nofranks:
            nofranks = int( self.get_argument( "n", 20))
            # d = restrict to document:
            restrictdnlist = []
            restrictdn = int( self.get_argument( "d", 0))
            if (restrictdn):
                restrictdnlist.append( restrictdn)
            # Evaluate query:
            start_time = time.time()
            (result,errormsg) = yield self.evaluateDymQuery( oldquerystr, querystr, nofranks, nofranks, restrictdnlist)
            time_elapsed = time.time() - start_time
            response = { 'error': errormsg,
                         'result': result,
                         'time': time_elapsed
            }
            self.write(response)
        except Exception as e:
            response = { 'error': str(e) }
            self.write(response)

# [2] Dispatcher:
application = tornado.web.Application([
    # /query in the URL triggers the handler for answering queries:
    (r"/query", QueryHandler),
    # /dym in the URL triggers the handler for answering queries:
    (r"/querydym", DymHandler),
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
        parser.add_option("-d", "--dymserver", dest="dymserver", default=dymserver,
                          help="Specify the address of the dym server as ADDR (default %s" % dymserver,
                          metavar="ADDR")

        (options, args) = parser.parse_args()
        myport = int(options.port)
        statserver = options.statserver
        if (statserver[0:].isdigit()):
            statserver = '{}:{}'.format( 'localhost', statserver)
        dymserver = options.dymserver
        if (dymserver[0:].isdigit()):
            dymserver = '{}:{}'.format( 'localhost', dymserver)

        # Positional arguments are storage server addresses, if empty use default at localhost:7184
        for arg in args:
            if (arg[0:].isdigit()):
                storageservers.append( '{}:{}'.format( 'localhost', arg))
            else:
                storageservers.append( arg)
        if (len( storageservers) == 0):
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


