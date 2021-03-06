import strus
import itertools
import heapq
import re
import collections
import utils

RankResult = collections.namedtuple('RankResult', ['docno','title','paratitle','weight','abstract','debuginfo'])
LinkResult = collections.namedtuple('LinkResult', ['docno','weight','links','features','titles'])

class Backend:
    # Create a query evaluation scheme:
    def createQueryEval( self, scheme):
        rt = self.context.createQueryEval()
        # Declare the sentence marker feature needed for abstracting:
        rt.addTerm( "sentence", "sent", "")
        rt.addTerm( "para", "para", "")

        # Declare the feature used for selecting result candidates:
        rt.addSelectionFeature( "selfeat")

        # Query evaluation scheme:
        if scheme == "BM25pff" or scheme == "BM25std":
            rt.addWeightingFunction( "BM25pff", {
                     "k1": 1.2, "b": 0.75, "avgdoclen": 1000,
                     "metadata_doclen": "doclen",
                     "titleinc": 4.0, "windowsize": 60, 'cardinality': "60%",
                     "ffbase": 0.4,
                     "maxdf": 0.2,
                     "para": {"feature":"para"}, "struct": {"feature":"sentence"}, "match": {"feature":"docfeat"}, "title": {"feature":"titlefield"},
                     "debug": "debug_weighting"
            })
            if scheme == "BM25std":
                rt.addWeightingFunction( "constant", {"precalc":1, "match": {"feature":"lnkfeat"}, "debug":"debug_weighting" })
                rt.defineWeightingFormula( "d * _1 + (1 - d) * _0", {"d": 0.7} )

        elif scheme == "BM25" or scheme == "BM25pg":
            rt.addWeightingFunction( "BM25", {
                     "k1": 1.2, "b": 0.75, "avgdoclen": 1000,
                     "metadata_doclen": "doclen",
                     "match": {"feature":"docfeat"},
                     "debug":"debug_weighting"
            })
            if scheme == "BM25pg":
                rt.addWeightingFunction( "metadata", {"name":"pageweight", "debug":"debug_weighting" })
                rt.defineWeightingFormula( "d * _0 * (_1 / 10) + (1 - d) * _0", {"d": 0.2} )

        elif scheme == "NBLNK" or scheme == "TILNK" or scheme == "VCLNK" or scheme == "STDLNK":
            rt.addWeightingFunction( "BM25", {
                     "k1": 1.2, "b": 0.75, "avgdoclen": 500,
                     "metadata_doclen": "doclen",
                     "match": {"feature":"docfeat"},
                     "debug":"debug_weighting"
            })
            rt.addWeightingFunction( "metadata", {"name": "pageweight", "debug":"debug_weighting" })
            rt.defineWeightingFormula( "d * _0 * (_1 / 10) + (1 - d) * _0", {"d": 0.2} )
        else:
            raise Exception( "unknown query evaluation scheme %s" % scheme)

        if scheme == "NBLNK":
            rt.addSummarizer( "accunear", {
                  "cofactor": 2.5, "type": "linkid", "range": 30, "cardinality": "75%",
                  "nofranks":20, "result":"LINK",
                  "match": {"feature":"docfeat"},
                  "debug": "debug_summarization"
            })
        elif scheme == "TILNK":
            rt.addSummarizer( "accunear", {
                  "cofactor": 2.5, "type": "veclfeat", "range": 30, "cardinality": "75%",
                  "nofranks":20, "result":"LINK",
                  "match": {"feature":"docfeat"},
                  "debug": "debug_summarization"
            })
        elif scheme == "VCLNK":
            rt.addSummarizer( "accunear", {
                  "cofactor": 2.5, "type": "vecfname", "range": 30, "cardinality": "75%",
                  "nofranks":20, "result":"LINK",
                  "match": {"feature":"docfeat"},
                  "debug": "debug_summarization"
            })
        elif scheme == "STDLNK":
            rt.addSummarizer( "accunear", {
                  "cofactor": 2.5, "type": "linkid", "range": 30, "cardinality": "75%",
                  "nofranks":20, "result":"TITLE",
                  "match": {"feature":"docfeat"},
                  "debug": "debug_summarization"
            })
            rt.addSummarizer( "accunear", {
                  "cofactor": 2.5, "type": "veclfeat", "range": 30, "cardinality": "75%",
                  "nofranks":20, "result":"LINK",
                  "match": {"feature":"docfeat"},
                  "debug": "debug_summarization"
            })
            rt.addSummarizer( "accunear", {
                  "cofactor": 2.5, "type": "vecfname", "range": 20, "cardinality": "75%",
                  "nofranks":20, "result":"VECTOR",
                  "match": {"feature":"docfeat"},
                  "debug": "debug_summarization"
            })
        else:
            # Summarizer for getting the document title:
            rt.addSummarizer( "attribute", { 
                  "name": "docid",
                  "debug": "debug_summarization"
            })
            # Summarizer for abstracting:
            rt.addSummarizer( "matchphrase", {
                  "type": "orig",
                  "windowsize": 40, "sentencesize": 100, "cardinality": "60%", "maxdf": 0.2,
                  "matchmark": '$<b>$</b>',
                  "struct": {"feature":"sentence"}, "match": {"feature":"docfeat"}, "para": {"feature":"para"}, "title": {"feature":"titlefield"},
                  "debug": "debug_summarization"
            })
        return rt

    # Constructor. Initializes the query evaluation schemes and the query and document analyzers:
    def __init__(self, config):
        # Open local storage on file with configuration specified:
        self.context = strus.Context()
        self.storage = self.context.createStorageClient( config )
        self.queryeval = {}
        for scheme in [ "BM25", "BM25pg", "BM25pff", "BM25std", "NBLNK", "TILNK", "VCLNK", "STDLNK" ]:
            self.queryeval[ scheme] = self.createQueryEval( scheme)

    def enableDebugTrace( self):
        self.context.enableDebugTrace( "query")
        self.context.enableDebugTrace( "analyzer")

    def printDebugTrace( self):
        print( "Query:\n%s\n" % utils.dumpTree( self.context.fetchDebugTrace()))

    def disableDebugTrace( self):
        self.context.disableDebugTrace( "query")
        self.context.disableDebugTrace( "analyzer")

    # Get pairs (a,b) of a and b in [0..N-1] with a < b:
    @staticmethod
    def getAscendingIndexPairs( N):
        for i in range(N):
            for k in range(i+1,N):
                yield [i,k]

    # Define features for weighting and summarization:
    def addFeatures( self, scheme, query, seltitle, terms, links, collectionsize):
        selexpr1 = []
        selexpr2 = []
        if seltitle == True:
            selfeat = []
            for term in terms:
                if term.df > 0.0:
                    selfeat.append( ["tist", term.value] )
            cardinality = 0
            if len( selfeat) >= 3:
                if len( selfeat) == 3:
                    cardinality = 2
                else:
                    cardinality = 3
            if selfeat:
                selexpr1 = ["contains",0,cardinality] + selfeat
        else:
            selfeat1 = []
            selfeat2 = []
            for term in terms:
                if term.df > 0.0:
                    selfeat1.append( [term.type, term.value] )
            if selfeat1:
                selexpr1 = ["contains"] + selfeat1
            if scheme != "NBLNK" and scheme != "TILNK" and scheme != "VCLNK" and scheme != "STDLNK" and selfeat2:
                selexpr2 = ["contains"] + selfeat2

        if not selexpr1 and not selexpr2:
            alltermstr = ""
            for term in terms:
                alltermstr += " %s '%s'" % (term.type, term.value)
            raise Exception( "query features %s not found in the collection" % alltermstr)

        for term in terms:
            query.addFeature( "docfeat", [term.type, term.value, term.length], term.weight)
            if term.df > 0.0:
                query.defineTermStatistics( term.type, term.value, {'df' : int(term.df)} )

        for link in links:
            query.addFeature( "lnkfeat", [link.type, link.value], link.weight)

        if selexpr1:
            query.addFeature( "selfeat", selexpr1, 1.0 )
        if selexpr2:
            query.addFeature( "selfeat", selexpr2, 1.0 )

        query.addFeature( "titlefield", {"from":"title_start", "to":"title_end"} )
        query.addMetaDataRestriction( ["==", "redirect", 0] )

    # Query evaluation scheme for a classical information retrieval query with BM25:
    def evaluateQuery( self, scheme, seltitle, terms, links, collectionsize, firstrank, nofranks, restrictset, debugtrace, with_debuginfo):
        if not scheme in self.queryeval:
            raise Exception( "unknown query evaluation scheme %s" % scheme)
        queryeval = self.queryeval[ scheme]
        query = queryeval.createQuery( self.storage)
        if len( terms) == 0:
            # Return empty result for empty query:
            return []

        self.addFeatures( scheme, query, seltitle, terms, links, collectionsize)
        query.setMaxNofRanks( nofranks)
        query.setMinRank( firstrank)
        query.defineGlobalStatistics( {'nofdocs' : collectionsize} )
        if restrictset:
            query.addDocumentEvaluationSet( restrictset )
        if with_debuginfo:
            query.setDebugMode( True)
        # Evaluate the query:
        result = query.evaluate()
        rt = []
        # Rewrite the results:
        if scheme == "NBLNK" or scheme == "TILNK" or scheme == "VCLNK":
            for rank in result.ranks:
                links = []
                for sumelem in rank.summary:
                    if sumelem.name == 'LINK':
                        links.append( [sumelem.value.strip(), sumelem.weight])
                rt.append( LinkResult( rank.docno, rank.weight, links, [], []))
        elif scheme == "STDLNK":
            for rank in result.ranks:
                links = []
                features = []
                titles = []
                for sumelem in rank.summary:
                    if sumelem.name == 'LINK':
                        links.append( [sumelem.value.strip(), sumelem.weight])
                    elif sumelem.name == 'TITLE':
                        titles.append( [sumelem.value.strip(), sumelem.weight])
                    elif sumelem.name == 'VECTOR':
                        features.append( [sumelem.value.strip(), sumelem.weight])
                rt.append( LinkResult( rank.docno, rank.weight, links, features, titles))
        else:
            if (debugtrace):
                print( "pass %u, nof matches %u" %(result.evalpass, result.nofranked))
            for rank in result.ranks:
                content = ""
                title = ""
                paratitle = None
                debug_weighting = None
                debug_summarization = None
                debuginfo = None
                for sumelem in rank.summary:
                    if sumelem.name == 'phrase' or sumelem.name == 'docstart':
                        if content != "":
                            content += ' --- '
                        content += sumelem.value
                    elif sumelem.name == 'para':
                        paratitle = sumelem.value
                    elif sumelem.name == 'docid':
                        title = sumelem.value
                    elif with_debuginfo:
                        if sumelem.name == 'debug_weighting':
                            if debug_weighting:
                                debug_weighting += sumelem.value
                            else:
                                debug_weighting = sumelem.value
                        elif sumelem.name == 'debug_summarization':
                            if debug_summarization:
                                debug_summarization += sumelem.value
                            else:
                                debug_summarization = sumelem.value
                if with_debuginfo:
                    if not debuginfo:
                        debuginfo = ""
                    if debug_weighting:
                        debuginfo += debug_weighting
                    if debug_summarization:
                        debuginfo += debug_summarization
                rt.append( RankResult( rank.docno, title, paratitle, rank.weight, content, debuginfo))
        return rt

    # Get an iterator on all absolute statistics of the storage
    def getInitStatisticsIterator( self):
        return self.storage.getAllStatistics( True)

    # Get an iterator on all absolute statistics of the storage
    def getDoneStatisticsIterator( self):
        return self.storage.getAllStatistics( False)

    # Get an iterator on statistic updates of the storage
    def getUpdateStatisticsIterator( self):
        return self.storage.getChangeStatistics()

    # Load the list of docno of documents with a pageweight higher than specified
    def getMinimumPageweightDocnos( self, minpageweight):
        rt = []
        browse = self.storage.createDocumentBrowser()
        browse.addMetaDataRestriction( [">=", "pageweight", minpageweight] )
        docno = browse.skipDoc( 0)
        while (docno > 0):
            rt.append( docno)
            docno = browse.skipDoc( docno+1)
        return rt

