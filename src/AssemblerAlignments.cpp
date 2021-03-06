#include "Assembler.hpp"
#include "DeBruijnGraph.hpp"
#include "compressAlignment.hpp"
#include "findLinearChains.hpp"
#include "MiniAssemblyMarkerGraph.hpp"
using namespace shasta;

namespace shasta {
    class AnalyzeAlignments2Graph;
    class AnalyzeAlignments3Graph;
}



class shasta::AnalyzeAlignments2Graph :
    public DeBruijnGraph<KmerId, 3, uint64_t> {
public:
    using SequenceId = uint64_t;
    void createVertexCoverageHistograms(
        vector<OrientedReadId>&,
        vector<uint64_t>& totalCoverageHistogram,
        vector<uint64_t>& sameStrandCoverageHistogram,
        vector<uint64_t>& oppositeStrandCoverageHistogram) const;
    void removeLowCoverageVertices(
        uint64_t minTotalCoverage,
        uint64_t minSameStrandCoverage,
        uint64_t minOppositeStrandCoverage,
        const vector<OrientedReadId>&);
    void writeGraphviz(
        const string& fileName,
        const vector<OrientedReadId>&,
        const vector<uint32_t>& firstOrdinal) const;
};



// Analyze the stored alignments involving a given oriented read.
void Assembler::analyzeAlignments(ReadId readId, Strand strand) const
{
    analyzeAlignments3(readId, strand);
}



// This version analyzes alignment coverage.
void Assembler::analyzeAlignments1(ReadId readId0, Strand strand0) const
{
    const OrientedReadId orientedReadId0(readId0, strand0);
    cout << "Analyzing stored alignments for " << orientedReadId0 << endl;

    // Get the alignments involving this oriented read.
    // This returns a vector alignments with swaps and/or
    // reverse complementing already done, as necessary.
    vector<StoredAlignmentInformation> alignments;
    getStoredAlignments(orientedReadId0, alignments);
    cout << "Found " << alignments.size() << " alignments." << endl;

    // Check that all alignments are strictly increasing.
    for(const auto& p: alignments) {
        p.alignment.checkStrictlyIncreasing();
    }



    // Create an ordinal table which contains, for each ordinal
    // of orientedReadId0, aligned ordinals for each of the aligned
    // oriented reads.
    const uint32_t markerCount0 = uint32_t(markers.size(orientedReadId0.getValue()));
    const uint32_t invalidOrdinal = std::numeric_limits<uint32_t>::max();
    vector< vector<uint32_t> > ordinalTable(
        markerCount0, vector<uint32_t>(alignments.size(), invalidOrdinal));
    for(uint64_t i=0; i<alignments.size(); i++) {
        const Alignment& alignment = alignments[i].alignment;
        for(const auto& o: alignment.ordinals) {
            const uint32_t ordinal0 = o[0];
            const uint32_t ordinal1 = o[1];
            SHASTA_ASSERT(ordinal0 < markerCount0);
            ordinalTable[ordinal0][i] = ordinal1;
        }
    }



    // Compute coverage for each marker and for each strand
    // (0=same strand, 1 = opposite strands).
    // Range coverage is the number of alignments whose range covers each ordinal.
    vector< array<uint32_t, 2> > coverage(markerCount0, {0, 0});
    vector< array<uint32_t, 2> > rangeCoverage(markerCount0, {0, 0});
    for(uint64_t i=0; i<alignments.size(); i++) {
        const OrientedReadId orientedReadId1 = alignments[i].orientedReadId;
        const Alignment& alignment = alignments[i].alignment;
        const auto strandIndex = (orientedReadId0.getStrand() == orientedReadId1.getStrand()) ? 0 : 1;

        // Update coverage for this alignment.
        for(const auto& o: alignment.ordinals) {
            const uint32_t ordinal0 = o[0];
            SHASTA_ASSERT(ordinal0 < markerCount0);
            ++coverage[ordinal0][strandIndex];
        }

        // Update range coverage for this alignment.
        for(uint32_t ordinal0=alignment.ordinals.front()[0];
            ordinal0<=alignment.ordinals.back()[0]; ordinal0++) {
            ++rangeCoverage[ordinal0][strandIndex];
        }
    }



    // Create the csv file and write the header.
    ofstream csv("Alignments.csv");
    csv << "Ordinal0,Coverage,Same strand coverage,Opposite strand coverage,"
        "Range coverage,Same strand range coverage,Opposite strand range coverage,"
        "Coverage ratio,Same strand coverage ratio,Opposite strand coverage ratio,";
    for(const auto& p: alignments) {
        csv << p.orientedReadId << ",";
    }
    csv << "\n";



    // Write the ordinal table to the csv file.
    for(uint32_t ordinal0=0; ordinal0<markerCount0; ordinal0++) {
        const uint64_t cSameStrand = coverage[ordinal0][0];
        const uint64_t cOppositeStrand = coverage[ordinal0][1];
        const uint64_t c = cSameStrand + cOppositeStrand;
        const uint64_t rcSameStrand = rangeCoverage[ordinal0][0];
        const uint64_t rcOppositeStrand = rangeCoverage[ordinal0][1];
        const uint64_t rc = rcSameStrand + rcOppositeStrand;
        const double rSameStrand  = double(cSameStrand) / double(rcSameStrand);
        const double rOppositeStrand  = double(cOppositeStrand) / double(rcOppositeStrand);
        const double r = double(c) / double(rc);

        csv << ordinal0 << ",";
        csv << c << ",";
        csv << cSameStrand << ",";
        csv << cOppositeStrand << ",";
        csv << rc << ",";
        csv << rcSameStrand << ",";
        csv << rcOppositeStrand << ",";
        csv << r << ",";
        csv << rSameStrand << ",";
        csv << rOppositeStrand << ",";
        for(uint64_t i=0; i<alignments.size(); i++) {
            const uint32_t ordinal1 = ordinalTable[ordinal0][i];
            if(ordinal1 != invalidOrdinal) {
                csv << ordinal1;
            } else {
                const Alignment& alignment = alignments[i].alignment;
                const uint32_t alignmentBegin0 = alignment.ordinals.front()[0];
                const uint32_t alignmentEnd0 = alignment.ordinals.back()[0];
                if((ordinal0 >= alignmentBegin0) and (ordinal0 <= alignmentEnd0)) {
                    csv << "No";
                }
            }
            csv << ",";
        }
        csv << "\n";
    }



    // Compute coverage histograms and write them out.
    // 0 = coverage
    // 1 = same strand coverage
    // 2 = opposite strand coverage.
    // 3 = range coverage
    // 4 = same strand range coverage
    // 5 = opposite strand range coverage.
    // Ratio histogram:
    // 0 = coverage ratio (binned).
    // 1 = same strand coverage ratio (binned).
    // 2 = opposite strand coverage ratio (binned).
    vector< array<uint64_t, 6> > histogram;
    const uint64_t binCount = 10;
    const double binSize = 1. / double(binCount);
    vector< array<uint64_t, 3> > ratioHistogram(binCount + 1, {0,0,0});
    for(uint32_t ordinal0=0; ordinal0<markerCount0; ordinal0++) {
        const uint64_t cSameStrand = coverage[ordinal0][0];
        const uint64_t cOppositeStrand = coverage[ordinal0][1];
        const uint64_t c = cSameStrand + cOppositeStrand;
        const uint64_t rcSameStrand = rangeCoverage[ordinal0][0];
        const uint64_t rcOppositeStrand = rangeCoverage[ordinal0][1];
        const uint64_t rc = rcSameStrand + rcOppositeStrand;
        const double rSameStrand  = (rcSameStrand==0 ? 0. : double(cSameStrand) / double(rcSameStrand));
        const double rOppositeStrand  = (rcOppositeStrand==0 ? 0. : double(cOppositeStrand) / double(rcOppositeStrand));
        const double r = (rc==0 ? 0. : double(c) / double(rc));
        const uint64_t irSameStrand = uint64_t(rSameStrand/binSize);
        const uint64_t irOppositeStrand = uint64_t(rOppositeStrand/binSize);
        const uint64_t ir = uint64_t(r/binSize);

        SHASTA_ASSERT(cSameStrand <= rcSameStrand);
        SHASTA_ASSERT(cOppositeStrand <= rcOppositeStrand);

        if(histogram.size() <= rc) {
            histogram.resize(rc + 1, {0,0,0,0,0,0,});
        }
        ++histogram[c][0];
        ++histogram[cSameStrand][1];
        ++histogram[cOppositeStrand][2];
        ++histogram[rc][3];
        ++histogram[rcSameStrand][4];
        ++histogram[rcOppositeStrand][5];
        ++ratioHistogram[ir][0];
        ++ratioHistogram[irSameStrand][1];
        ++ratioHistogram[irOppositeStrand][2];
    }
    ofstream csv2("AlignmentCoverageHistogram.csv");
    csv2 << "Coverage value,Total,Same strand,Opposite strand,"
        "Range total, Range same strand, Range opposite strand\n";
    for(uint64_t c=0; c<histogram.size(); c++) {
        csv2 << c << ",";
        for(uint64_t i=0; i<6; i++) {
            csv2 << histogram[c][i] << ",";
        }
        csv2 << "\n";
    }
    ofstream csv3("AlignmentCoverageRatioHistogram.csv");
    csv3 << "Coverage ratio,Total,Same strand,Opposite strand\n";
    for(uint64_t c=0; c<ratioHistogram.size(); c++) {
        csv3 << double(c)*binSize << ",";
        for(uint64_t i=0; i<3; i++) {
            csv3 << ratioHistogram[c][i] << ",";
        }
        csv3 << "\n";
    }
}



// Get the stored compressed alignments involving a given oriented read.
// This performs swaps and reverse complementing as necessary,
// To return alignments in which the first oriented read is
// the one specified as the argument.
void Assembler::getStoredAlignments(
    OrientedReadId orientedReadId0,
    vector<StoredAlignmentInformation> & alignments) const
{
    // Check that we have what we need.
    checkMarkersAreOpen();
    checkAlignmentDataAreOpen();
    SHASTA_ASSERT(compressedAlignments.isOpen());

    // Access the alignment table portion for this oriented read.
    // It contains indexes into alignmentData and compressedAlignments
    // for alignments involving this oriented read.
    const span<const uint32_t> alignmentIndexes = alignmentTable[orientedReadId0.getValue()];



    // Loop over alignments involving this oriented read.
    alignments.clear();
    for(const uint32_t alignmentIndex: alignmentIndexes) {

        // Access the stored information we have about this alignment.
        AlignmentData alignmentData = this->alignmentData[alignmentIndex];
        const span<const char> compressedAlignment = compressedAlignments[alignmentIndex];

        // The alignment is stored with its first read on strand 0.
        OrientedReadId alignmentOrientedReadId0(alignmentData.readIds[0], 0);
        OrientedReadId alignmentOrientedReadId1(alignmentData.readIds[1],
            alignmentData.isSameStrand ? 0 : 1);

        // Decompress the alignment.
        alignments.resize(alignments.size() + 1);
        Alignment& alignment = alignments.back().alignment;
        OrientedReadId& orientedReadId1 = alignments.back().orientedReadId;
        alignments.back().alignmentId = alignmentIndex;
        decompress(compressedAlignment, alignment);
        SHASTA_ASSERT(alignment.ordinals.size() == alignmentData.info.markerCount);



        // Tweak the alignment to make sure its first oriented read is orientedReadId0.
        // This may require a swap and/or reverse complement.

        // Do a swap, if needed.
        if(alignmentOrientedReadId0.getReadId() != orientedReadId0.getReadId()) {
            alignment.swap();
            swap(alignmentOrientedReadId0, alignmentOrientedReadId1);
        }
        SHASTA_ASSERT(alignmentOrientedReadId0.getReadId() == orientedReadId0.getReadId());

        // Reverse complement, if needed.
        if(alignmentOrientedReadId0.getStrand() != orientedReadId0.getStrand()) {
            alignment.reverseComplement(
                uint32_t(markers.size(alignmentOrientedReadId0.getValue())),
                uint32_t(markers.size(alignmentOrientedReadId1.getValue())));
            alignmentOrientedReadId0.flipStrand();
            alignmentOrientedReadId1.flipStrand();
        }
        SHASTA_ASSERT(alignmentOrientedReadId0 == orientedReadId0);
        orientedReadId1 = alignmentOrientedReadId1;
    }
}



// This version of getStoredAlignments only returns alignments in which
// the second oriented read is present in a given vector orientedReadIds1,
// which is required to be sorted.
void Assembler::getStoredAlignments(
    OrientedReadId orientedReadId0,
    const vector<OrientedReadId>& orientedReadIds1,
    vector<StoredAlignmentInformation>& alignments) const
{
    // Check that orientedReadIds1 is sorted.
    for(uint64_t i=1; i<orientedReadIds1.size(); i++) {
        SHASTA_ASSERT(orientedReadIds1[i-1] < orientedReadIds1[i]);
    }

    // Check that we have what we need.
    checkMarkersAreOpen();
    checkAlignmentDataAreOpen();
    SHASTA_ASSERT(compressedAlignments.isOpen());

    // Access the alignment table portion for this oriented read.
    // It contains indexes into alignmentData and compressedAlignments
    // for alignments involving this oriented read.
    const span<const uint32_t> alignmentIds = alignmentTable[orientedReadId0.getValue()];



    // Loop over alignments involving this oriented read.
    alignments.clear();
    for(const uint32_t alignmentId: alignmentIds) {
        AlignmentData alignmentData = this->alignmentData[alignmentId];

        // The alignment is stored with its first read on strand 0.
        OrientedReadId alignmentOrientedReadId0(alignmentData.readIds[0], 0);
        OrientedReadId alignmentOrientedReadId1(alignmentData.readIds[1],
            alignmentData.isSameStrand ? 0 : 1);

        // Tweak the alignment to make sure its first oriented read is orientedReadId0.
        // This may require a swap and/or reverse complement.

        // Do a swap, if needed.
        bool doSwap = false;
        if(alignmentOrientedReadId0.getReadId() != orientedReadId0.getReadId()) {
            doSwap = true;
            swap(alignmentOrientedReadId0, alignmentOrientedReadId1);
        }
        SHASTA_ASSERT(alignmentOrientedReadId0.getReadId() == orientedReadId0.getReadId());

        // Reverse complement, if needed.
        bool doReverseComplement = false;
        if(alignmentOrientedReadId0.getStrand() != orientedReadId0.getStrand()) {
            doReverseComplement = true;
            alignmentOrientedReadId0.flipStrand();
            alignmentOrientedReadId1.flipStrand();
        }

        SHASTA_ASSERT(alignmentOrientedReadId0 == orientedReadId0);
        const OrientedReadId orientedReadId1 = alignmentOrientedReadId1;

        // If orientedReadId1 is not one of the oriented reads we are interested in, skip.
        if(not binary_search(orientedReadIds1.begin(), orientedReadIds1.end(), orientedReadId1)) {
            continue;
        }

        // Decompress the alignment.
        alignments.resize(alignments.size() + 1);
        StoredAlignmentInformation& storedAlignmentInformation = alignments.back();
        storedAlignmentInformation.alignmentId = alignmentId;
        storedAlignmentInformation.orientedReadId = orientedReadId1;
        const span<const char> compressedAlignment = compressedAlignments[alignmentId];
        Alignment& alignment = alignments.back().alignment;
        decompress(compressedAlignment, alignment);
        SHASTA_ASSERT(alignment.ordinals.size() == alignmentData.info.markerCount);

        // Tweak the alignment consistently with what we did above.
        if(doSwap) {
            alignment.swap();
        }
        if(doReverseComplement) {
            alignment.reverseComplement(
                uint32_t(markers.size(alignmentOrientedReadId0.getValue())),
                uint32_t(markers.size(alignmentOrientedReadId1.getValue())));
        }
    }
}



// This version uses a De Bruijn graph to do a mini-assembly
// using only this oriented read and the aligned portions
// of oriented reads for which we have an alignment with this one.
void Assembler::analyzeAlignments2(ReadId readId0, Strand strand0) const
{
    // Parameters controlling this function.
    // Expose when code stabilizes.
    const uint64_t minTotalCoverage = 5;
    const uint64_t minSameStrandCoverage = 2;
    const uint64_t minOppositeStrandCoverage = 2;
    // const double similarityThreshold = 0.75;
    const uint64_t neighborCount = 3;

    // Get the alignments of this oriented read, with the proper orientation,
    // and with this oriented read as the first oriented read in the alignment.
    const OrientedReadId orientedReadId0(readId0, strand0);
    const vector< pair<OrientedReadId, AlignmentInfo> > alignments =
        findOrientedAlignments(orientedReadId0);
    cout << "Found " << alignments.size() << " alignments." << endl;



    // We will do a small assembly for the marker sequence of this oriented read
    // plus the aligned portions of the marker sequences of aligned reads.
    // Gather these sequences.
    // The marker sequence for this oriented read is stored
    // at the last position of this vector.
    using Sequence = vector<KmerId>;
    using SequenceId = uint64_t;
    vector<Sequence> sequences(alignments.size() + 1);
    vector<OrientedReadId> orientedReadIds(sequences.size());
    vector<uint32_t> firstOrdinals(sequences.size());
    for(SequenceId sequenceId=0; sequenceId<alignments.size(); sequenceId++) {
        Sequence& sequence = sequences[sequenceId];
        const OrientedReadId orientedReadId1 = alignments[sequenceId].first;
        orientedReadIds[sequenceId] = orientedReadId1;
        const span<const CompressedMarker> markers1 = markers[orientedReadId1.getValue()];
        const AlignmentInfo& alignmentInfo = alignments[sequenceId].second;
        const uint32_t first1 = alignmentInfo.data[1].firstOrdinal;
        firstOrdinals[sequenceId] = first1;
        const uint32_t last1 = alignmentInfo.data[1].lastOrdinal;
        sequence.resize(last1 + 1 - first1);
        for(uint64_t i=0; i<sequence.size(); i++) {
            sequence[i] = markers1[first1 + i].kmerId;
        }
    }
    Sequence& sequence0 = sequences.back();
    orientedReadIds.back() = orientedReadId0;
    firstOrdinals.back() = 0;
    const span<const CompressedMarker> markers0 = markers[orientedReadId0.getValue()];
    const uint64_t markerCount0 = markers0.size();
    sequence0.resize(markerCount0);
    for(uint32_t ordinal=0; ordinal!=markerCount0; ordinal++) {
        sequence0[ordinal] = markers0[ordinal].kmerId;
    }
    cout << orientedReadId0 << " has " << markerCount0 << " markers." << endl;



    // Create the De Bruijn graph.
    // Use as SequenceId the index into the above vector of sequences.
    using Graph = AnalyzeAlignments2Graph;
    Graph graph;
    for(SequenceId sequenceId=0; sequenceId<sequences.size(); sequenceId++) {
        graph.addSequence(sequenceId, sequences[sequenceId]);
    }
    graph.removeAmbiguousVertices();

    // Before removing vertices based on coverage, create a coverage histogram and write it out.
    vector<uint64_t> totalCoverageHistogram;
    vector<uint64_t> sameStrandCoverageHistogram;
    vector<uint64_t> oppositeStrandCoverageHistogram;
    graph.createVertexCoverageHistograms(
        orientedReadIds,
        totalCoverageHistogram,
        sameStrandCoverageHistogram,
        oppositeStrandCoverageHistogram);
    {
        ofstream csv("DeBruijnGraphCoverageHistogram.csv");
        csv << "Coverage,Total coverage frequency,"
            "Same strand coverage frequency,Opposite strand coverage frequency\n";
        const uint64_t maxCoverage = max(totalCoverageHistogram.size(),
            max(sameStrandCoverageHistogram.size(),
            oppositeStrandCoverageHistogram.size()));
        for(uint64_t coverage=0; coverage<maxCoverage; coverage++) {
            csv << coverage << ",";

            if(coverage < totalCoverageHistogram.size()) {
                csv << totalCoverageHistogram[coverage];
            } else {
                csv << "0";
            }
            csv << ",";

            if(coverage < sameStrandCoverageHistogram.size()) {
                csv << sameStrandCoverageHistogram[coverage];
            } else {
                csv << "0";
            }
            csv << ",";

            if(coverage < oppositeStrandCoverageHistogram.size()) {
                csv << oppositeStrandCoverageHistogram[coverage];
            } else {
                csv << "0";
            }
            csv << "\n";
        }
    }

    // Finish creation of the De Bruijn graph.
    graph.removeLowCoverageVertices(
        minTotalCoverage,
        minSameStrandCoverage,
        minOppositeStrandCoverage,
        orientedReadIds);
    graph.createEdges();
    cout << "The De Bruijn graph has " << num_vertices(graph) <<
        " vertices and " << num_edges(graph) << " edges." << endl;
    graph.writeGraphviz("DeBruijnGraph.dot", orientedReadIds, firstOrdinals);


    // Find sets of incompatible vertices.
    std::set< std::set<Graph::vertex_descriptor> > incompatibleVertexSets;
    graph.findIncompatibleVertexSets(incompatibleVertexSets);
    cout << "Found " << incompatibleVertexSets.size() << " incompatible vertex sets." << endl;



    // For each set of incompatible vertices,
    // construct a signature vector that tells us which of the incompatible vertices
    // each reads appears in, if any.
    // >=0: Gives the index of the vertex (in the incompatible set) in which the read appears.
    // -1 = Read does not appear in the incompatible vertex set.
    // -2 = Read appears more than once in the incompatible vertex set.
    vector< vector<int64_t> > signatures(
        incompatibleVertexSets.size(), vector<int64_t>(sequences.size(), -1));

    uint64_t i = 0;
    for(const auto& incompatibleVertexSet : incompatibleVertexSets) {

        /*
        cout << "Incompatible vertex set with " <<
            incompatibleVertexSet.size() << " vertices:" << endl;
        for(const Graph::vertex_descriptor v: incompatibleVertexSet) {

            cout << "Vertex " << graph[v].vertexId << endl;
            for(const auto& p: graph[v].occurrences) {
                const SequenceId sequenceId = p.first;
                const uint64_t ordinal = firstOrdinals[sequenceId] + p.second;
                cout << sequenceId << " " << orientedReadIds[sequenceId] << " " << ordinal << endl;
            }
        }
        */

        // Copy the set to a vector for ease in manipulating.
        vector<Graph::vertex_descriptor> incompatibleVertexVector(incompatibleVertexSet.size());
        copy(incompatibleVertexSet.begin(), incompatibleVertexSet.end(), incompatibleVertexVector.begin());

        // Find out in which branch each sequence appears.
        // -1 = does not appear.
        // -2 = appears in multiple branches.
        vector<int64_t>& signature = signatures[i];
        for(uint64_t branch=0; branch<incompatibleVertexVector.size(); branch++) {
            for(const auto& p: graph[incompatibleVertexVector[branch]].occurrences) {
                const SequenceId sequenceId = p.first;
                const int64_t oldValue = signature[sequenceId];
                if(oldValue == -2) {
                    // Do nothing.
                } else if(oldValue == -1) {
                    signature[sequenceId] = branch; // This is the first time we see it.
                } else {
                    signature[sequenceId] = -2;     // We have already seen it.
                    SHASTA_ASSERT(0);   // findIncompatibleVertexSets should never generate this.
                }
            }
        }

        for(const int64_t branch: signature) {
            if(branch == -2) {
                cout << "?";
            } else if(branch == -1) {
                cout << ".";
            } else {
                cout << branch;
            }
        }
        cout << endl;

        ++i;
    }



    // Compute the number of times each pair appears on the same side or
    // different sides of a bubble.
    vector< vector<uint64_t> > sameBranchMatrix(sequences.size(), vector<uint64_t>(sequences.size()));
    vector< vector<uint64_t> > differentBranchMatrix(sequences.size(), vector<uint64_t>(sequences.size()));
    for(SequenceId sequenceId0=0; sequenceId0<sequences.size(); sequenceId0++) {
        for(SequenceId sequenceId1=sequenceId0+1; sequenceId1<sequences.size(); sequenceId1++) {
            uint64_t sameBranchCount = 0;
            uint64_t differentBranchCount = 0;
            for(const auto& signature: signatures) {
                const int64_t s0 = signature[sequenceId0];
                const int64_t s1 = signature[sequenceId1];
                if(s0<0 or s1<0) {
                    continue;
                }
                if(s0 == s1) {
                    ++sameBranchCount;
                } else {
                    ++differentBranchCount;
                }
            }

            sameBranchMatrix[sequenceId0][sequenceId1] = sameBranchCount;
            sameBranchMatrix[sequenceId1][sequenceId0] = sameBranchCount;
            differentBranchMatrix[sequenceId0][sequenceId1] = differentBranchCount;
            differentBranchMatrix[sequenceId1][sequenceId0] = differentBranchCount;
        }
    }



    // Write out the matrices.
    {
        ofstream csv("MiniAssembly-Similarity.csv");
        for(SequenceId sequenceId0=0; sequenceId0<sequences.size(); sequenceId0++) {
            for(SequenceId sequenceId1=0; sequenceId1<sequences.size(); sequenceId1++) {
                const uint64_t sameBranchCount = sameBranchMatrix[sequenceId0][sequenceId1];
                const uint64_t differentBranchCount = differentBranchMatrix[sequenceId0][sequenceId1];
                const uint64_t totalCount = sameBranchCount + differentBranchCount;
                const double similarity = double(sameBranchCount) / double(totalCount);
                csv << sameBranchCount << "/";
                csv << differentBranchCount << "/";
                csv << similarity << ",";
            }
            csv << "\n";
        }
    }



    // To decide which edges to draw, sort matrix entries by delta=sameBranchCount-differentBranchCount
    // and keep the best neighborCount*sequences.size() / 2.
    // Store pair(delta, pair(sequenceId0, sequenceId1)).
    vector< pair<int64_t, pair<uint64_t, uint64_t> > > edgeTable;
    for(SequenceId sequenceId0=0; sequenceId0<sequences.size(); sequenceId0++) {
        for(SequenceId sequenceId1=sequenceId0+1; sequenceId1<sequences.size(); sequenceId1++) {
            const uint64_t sameBranchCount = sameBranchMatrix[sequenceId0][sequenceId1];
            const uint64_t differentBranchCount = differentBranchMatrix[sequenceId0][sequenceId1];
            const int64_t delta = int64_t(sameBranchCount) -int64_t(differentBranchCount);
            edgeTable.push_back(make_pair(delta, make_pair(sequenceId0, sequenceId1)));
        }
    }
    sort(edgeTable.begin(), edgeTable.end(),
        std::greater< pair<int64_t, pair<uint64_t, uint64_t> > >());
    const uint64_t keepCount = neighborCount * sequences.size() / 2;
    if(edgeTable.size() > keepCount) {
        edgeTable.resize(keepCount);
    }



    // Write a read similarity graph to represent the above matrices.
    {
        ofstream out("MiniAssembly-ReadSimilarityGraph.dot");
        out << "graph G{\n";



        // Draw vertices.
        for(SequenceId sequenceId0=0; sequenceId0<sequences.size(); sequenceId0++) {
            const uint64_t sameBranchCount = sameBranchMatrix[sequenceId0][sequences.size()-1];
            const uint64_t differentBranchCount = differentBranchMatrix[sequenceId0][sequences.size()-1];
            out << sequenceId0;
            out << "[";

            // Vertex tooltip.
            out << "tooltip=\"" << sequenceId0 << " " << orientedReadIds[sequenceId0] <<
                ": same branch " << sameBranchCount <<
                ": different branch " << differentBranchCount <<
                "\"";

            // Vertex color.
            if(sequenceId0 == sequences.size()-1) {
                out << " color=cyan";
            } else {
                if(differentBranchCount > sameBranchCount) {
                    out << " color=red";
                } else if(differentBranchCount == sameBranchCount) {
                    out << " color=orange";
                }
            }
            out << "]\n";
        }



        // Draw edges.
        for(const auto& p: edgeTable) {
            const SequenceId sequenceId0 = p.second.first;
            const SequenceId sequenceId1 = p.second.second;
            // const uint64_t sameBranchCount = sameBranchMatrix[sequenceId0][sequenceId1];
            // const uint64_t differentBranchCount = differentBranchMatrix[sequenceId0][sequenceId1];
            // const uint64_t totalCount = sameBranchCount + differentBranchCount;
            // const double similarity = double(sameBranchCount) / double(totalCount);
            out << sequenceId0 << "--" << sequenceId1;
            out << " [";

            // Edge thickness.
            // out << " penwidth=" << 0.2*double(sameBranchCount - differentBranchCount);

            // Edge color.
            // out << " color=\"" << (similarity-0.5)/1.5 << " 1. 1.\"";

            out << "]";
            out << ";\n";
        }

        out << "}\n";
    }


#if 0
    // Spectral drawing using as similarity the difference sameBranchCount-differentBranchCount.
    SimilarityMatrix similarityMatrix(sequences.size());
    for(SequenceId sequenceId0=0; sequenceId0<sequences.size(); sequenceId0++) {
        for(SequenceId sequenceId1=0; sequenceId1<sequences.size(); sequenceId1++) {
            const int64_t sameBranchCount = sameBranchMatrix[sequenceId0][sequenceId1];
            const int64_t differentBranchCount = differentBranchMatrix[sequenceId0][sequenceId1];
            if(sequenceId0 ==sequenceId1) {
                similarityMatrix[sequenceId0][sequenceId1]= 0.;
            } else {
                similarityMatrix[sequenceId0][sequenceId1] = double(sameBranchCount - differentBranchCount);
            }
        }
    }
    SpectralDrawingResults spectralDrawingResults;
    similarityMatrix.draw(2, spectralDrawingResults);
#endif

}


void AnalyzeAlignments2Graph::createVertexCoverageHistograms(
    vector<OrientedReadId> & orientedReadIds,
    vector<uint64_t>& totalCoverageHistogram,
    vector<uint64_t>& sameStrandCoverageHistogram,
    vector<uint64_t>& oppositeStrandCoverageHistogram
    ) const
{
    const Graph& graph = *this;

    const Strand strand0 = orientedReadIds.back().getStrand();

    totalCoverageHistogram.clear();
    sameStrandCoverageHistogram.clear();
    oppositeStrandCoverageHistogram.clear();
    BGL_FORALL_VERTICES_T(v, graph, Graph) {

        // Total coverage.
        const uint64_t totalCoverage = graph[v].occurrences.size();
        if(totalCoverage >= totalCoverageHistogram.size()) {
            totalCoverageHistogram.resize(totalCoverage + 1, 0);
        }
        ++totalCoverageHistogram[totalCoverage];

        // Compute per strand coverage.
        array<uint64_t, 2>coveragePerStrand = {0, 0};
        for(const auto& p: graph[v].occurrences) {
            const SequenceId sequenceId = p.first;
            const OrientedReadId orientedReadId = orientedReadIds[sequenceId];
            ++coveragePerStrand[orientedReadId.getStrand()];
        }
        SHASTA_ASSERT(coveragePerStrand[0] + coveragePerStrand[1] == totalCoverage);

        // Same strand coverage
        uint64_t c = coveragePerStrand[strand0];
        if(c >= sameStrandCoverageHistogram.size()) {
            sameStrandCoverageHistogram.resize(c + 1, 0);
        }
        ++sameStrandCoverageHistogram[c];

        // Opposite strand coverage
        c = coveragePerStrand[1-strand0];
        if(c >= oppositeStrandCoverageHistogram.size()) {
            oppositeStrandCoverageHistogram.resize(c + 1, 0);
        }
        ++oppositeStrandCoverageHistogram[c];

    }

}



void AnalyzeAlignments2Graph::removeLowCoverageVertices(
    uint64_t minTotalCoverage,
    uint64_t minSameStrandCoverage,
    uint64_t minOppositeStrandCoverage,
    const vector<OrientedReadId>& orientedReadIds)
{
    Graph& graph = *this;
    using SequenceId = uint64_t;

    const Strand strand0 = orientedReadIds.back().getStrand();


    // Gather the vertices to be removed.
    vector<vertex_descriptor> verticesTobeRemoved;
    BGL_FORALL_VERTICES_T(v, graph, Graph) {

        if(graph[v].occurrences.size() < minTotalCoverage) {

            // Total coverage is too low.
            verticesTobeRemoved.push_back(v);

        } else {

            // Total coverage is sufficient. Check coverage per strand.
            array<uint64_t, 2>coveragePerStrand = {0, 0};
            for(const auto& p: graph[v].occurrences) {
                const SequenceId sequenceId = p.first;
                const OrientedReadId orientedReadId = orientedReadIds[sequenceId];
                ++coveragePerStrand[orientedReadId.getStrand()];
            }

            if(
                (coveragePerStrand[strand0] < minSameStrandCoverage) or
                (coveragePerStrand[1 - strand0] < minOppositeStrandCoverage)) {
                verticesTobeRemoved.push_back(v);
            }
        }
    }



    for(const vertex_descriptor v: verticesTobeRemoved) {
        clear_vertex(v, graph);
        remove_vertex(v, graph);
    }

}


void AnalyzeAlignments2Graph::writeGraphviz(
    const string& fileName,
    const vector<OrientedReadId>& orientedReadIds,
    const vector<uint32_t>& firstOrdinals) const
{
    const Graph& graph = *this;
    ofstream s(fileName);

    s << "digraph DeBruijnGraph {\n";



    BGL_FORALL_VERTICES_T(v, graph, Graph) {
        s << graph[v].vertexId << "[";

        // Label.
        s << "label=\""  << graph[v].vertexId;
        for(const auto& occurrence: graph[v].occurrences) {
            const uint64_t sequenceId = occurrence.first;
            const uint64_t ordinal = occurrence.second;
            s << "\\n" << orientedReadIds[sequenceId] << ":" <<
                firstOrdinals[sequenceId] + ordinal;
        }
        s << "\"";

        if(graph[v].occurrences.back().first == orientedReadIds.size()-1) {
            s << "style=filled fillcolor=pink";
        }

        s << "];\n";
    }



    BGL_FORALL_EDGES_T(e, graph, Graph) {
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        s << graph[v0].vertexId << "->";
        s << graph[v1].vertexId << ";\n";
    }

    s << "}\n";

}



// This version uses a marker graph graph to do a mini-assembly
// using only this oriented read and the aligned portions
// of oriented reads for which we have an alignment with this one.
// Like analyzeAlignments2, but using a marker graph
// (class MiniAssemblyMarkerGraph) instead of a De Bruijn graph.
void Assembler::analyzeAlignments3(ReadId readId0, Strand strand0) const
{
    // Parameters controlling this function.
    // Expose when code stabilizes.
    const uint64_t minTotalEdgeCoverage = 5;
    const uint64_t minPerStrandEdgeCoverage = 2;




    // Get the alignments involving this oriented read.
    // This returns a vector alignments with swaps and/or
    // reverse complementing already done, as necessary.
    vector<StoredAlignmentInformation> alignments;
    const OrientedReadId orientedReadId0(readId0, strand0);
    getStoredAlignments(orientedReadId0, alignments);

    // Check that all alignments are strictly increasing.
    for(const auto& p: alignments) {
        p.alignment.checkStrictlyIncreasing();
    }



    // We will do a small assembly for the marker sequence of this oriented read
    // plus the aligned portions of the marker sequences of aligned reads.
    // Gather these sequences.
    // The marker sequence for this oriented read is stored
    // at the last position of this vector.
    using Sequence = vector<KmerId>;
    using SequenceId = uint64_t;
    vector<Sequence> sequences(alignments.size() + 1);
    vector<OrientedReadId> orientedReadIds(sequences.size());
    vector<uint32_t> firstOrdinals(sequences.size());
    vector<uint32_t> lastOrdinals(sequences.size());
    for(SequenceId sequenceId=0; sequenceId<alignments.size(); sequenceId++) {
        Sequence& sequence = sequences[sequenceId];
        const OrientedReadId orientedReadId1 = alignments[sequenceId].orientedReadId;
        orientedReadIds[sequenceId] = orientedReadId1;
        const span<const CompressedMarker> markers1 = markers[orientedReadId1.getValue()];
        const Alignment& alignment = alignments[sequenceId].alignment;
        const uint32_t first1 = alignment.ordinals.front()[1];
        firstOrdinals[sequenceId] = first1;
        const uint32_t last1 = alignment.ordinals.back()[1];
        lastOrdinals[sequenceId] = last1;
        sequence.resize(last1 + 1 - first1);
        for(uint64_t i=0; i<sequence.size(); i++) {
            sequence[i] = markers1[first1 + i].kmerId;
        }
    }

    // Add the sequence of the oriented read we started from.
    Sequence& sequence0 = sequences.back();
    const SequenceId sequenceId0 = sequences.size() - 1;
    orientedReadIds.back() = orientedReadId0;
    const span<const CompressedMarker> markers0 = markers[orientedReadId0.getValue()];
    const uint64_t markerCount0 = markers0.size();
    firstOrdinals.back() = 0;
    lastOrdinals.back() = uint32_t(markers0.size() - 1);
    sequence0.resize(markerCount0);
    for(uint32_t ordinal=0; ordinal!=markerCount0; ordinal++) {
        sequence0[ordinal] = markers0[ordinal].kmerId;
    }
    cout << orientedReadId0 << " has " << markerCount0 << " markers, " <<
        alignments.size() << " stored alignments." << endl;



    // Create a marker graph of these sequences.
    // Use as SequenceId the index into the sequences vector.
    using Graph = MiniAssemblyMarkerGraph;
    using vertex_descriptor = Graph::vertex_descriptor;
    // using edge_descriptor = Graph::edge_descriptor;
    Graph graph(orientedReadIds);
    for(SequenceId sequenceId=0; sequenceId<sequences.size(); sequenceId++) {
        graph.addSequence(sequenceId, sequences[sequenceId]);
    }
    const uint64_t disjointSetsSize = graph.doneAddingSequences();
    cout << "The disjoint set data structure has size " << disjointSetsSize << endl;



    // Merge pairs of aligned markers.
    vector< pair<uint64_t, uint64_t> > v;
    for(SequenceId sequenceId1=0; sequenceId1<alignments.size(); sequenceId1++) {
        const Alignment& alignment = alignments[sequenceId1].alignment;
        v.clear();
        for(const auto& ordinals: alignment.ordinals) {
            // Merge ordinals relative to the start of the portion of
            // each sequence used in the mini-assembly.
            v.push_back({
                ordinals[0] - firstOrdinals[sequenceId0],
                ordinals[1] - firstOrdinals[sequenceId1]});
            SHASTA_ASSERT(
                markers[orientedReadIds[sequenceId0].getValue()][ordinals[0]].kmerId ==
                markers[orientedReadIds[sequenceId1].getValue()][ordinals[1]].kmerId
                );
        }
        graph.merge(sequenceId0, sequenceId1, v);
    }



    // We also need to merge vertices using alignments between the oriented reads
    // aligned with orientedReadId0.
    // Just for this portion of the code, take orientedReadId0 out of the orientedReadIds
    // vector.
    orientedReadIds.resize(alignments.size());
    for(SequenceId sequenceId1=0; sequenceId1<alignments.size(); sequenceId1++) {
        const OrientedReadId orientedReadId1 = orientedReadIds[sequenceId1];

        // Get alignments between orientedReadId1 and the other oriented reads in
        // orientedReadIds.
        getStoredAlignments(orientedReadId1, orientedReadIds, alignments);

        // Loop over the alignments we got.
        for(const auto& storedAlignment: alignments) {
            const OrientedReadId orientedReadId2 = storedAlignment.orientedReadId;

            // Look up the corresponding SequenceId.
            const auto it = std::lower_bound(orientedReadIds.begin(), orientedReadIds.end(),
                orientedReadId2);
            SHASTA_ASSERT(it != orientedReadIds.end());
            const SequenceId sequenceId2 = it - orientedReadIds.begin();

            // Merge vertices.
            const Alignment& alignment = storedAlignment.alignment;
            v.clear();
            for(const auto& ordinals: alignment.ordinals) {
                if(ordinals[0] < firstOrdinals[sequenceId1]) {
                    continue;
                }
                if(ordinals[1] < firstOrdinals[sequenceId2]) {
                    continue;
                }
                if(ordinals[0] > lastOrdinals[sequenceId1]) {
                    continue;
                }
                if(ordinals[1] > lastOrdinals[sequenceId2]) {
                    continue;
                }
                v.push_back({
                    ordinals[0] - firstOrdinals[sequenceId1],
                    ordinals[1] - firstOrdinals[sequenceId2]});
                SHASTA_ASSERT(
                    markers[orientedReadIds[sequenceId1].getValue()][ordinals[0]].kmerId ==
                    markers[orientedReadIds[sequenceId2].getValue()][ordinals[1]].kmerId
                    );
            }
            graph.merge(sequenceId1, sequenceId2, v);
        }
    }
    // Add orientedReadId0 back to our list.
    orientedReadIds.push_back(orientedReadId0);



    // Finish creation of the marker graph.
    graph.doneMerging();
    graph.removeSelfEdges();
    graph.removeLowCoverageEdges(minTotalEdgeCoverage, minPerStrandEdgeCoverage);
    graph.removeIsolatedVertices();
    graph.findBubbles();
    cout << "The marker graph for the mini-assembly has " << num_vertices(graph) <<
        " vertices, " << num_edges(graph) << " edges, and " <<
        graph.bubbles.size() << " bubbles." << endl;


    // Write out bubble branch tables.
    cout << "Branch tables for " << graph.bubbles.size() << " bubbles:" << endl;
    for(const Graph::Bubble& bubble: graph.bubbles) {
        /*
        cout << "Bubble with " << bubble.branches.size() << " branches." << endl;
        for(uint64_t branchId=0; branchId<bubble.branches.size(); branchId++) {
            for(SequenceId sequenceId=0; sequenceId<orientedReadIds.size(); sequenceId++) {
                cout << (bubble.contains[sequenceId][branchId] ? '*' : '.');
            }
            cout << endl;
        }
        */
        for(SequenceId sequenceId=0; sequenceId<orientedReadIds.size(); sequenceId++) {
            const int64_t value = bubble.branchTable[sequenceId];
            SHASTA_ASSERT(value < 10);
            if(value < 0) {
                cout << '.';
            } else {
                cout << value;
            }
        }
        cout << " " << bubble.branches.size() << endl;
    }



    // Count how many time each oriented read appears in the same
    // or different bubble as orientedReadId0.
    ofstream bubbleCsv("BubbleSummary.csv");
    bubbleCsv << "SequenceId,OrientedReadId,"
        "SameBubbleCount,DifferentBubbleCount,TotalCount,"
        "SameBubbleRatio,DifferentBubbleRatio\n";
    for(SequenceId sequenceId1=0; sequenceId1<orientedReadIds.size()-1; sequenceId1++) {
        uint64_t sameCount = 0;
        uint64_t differentCount = 0;
        for(const Graph::Bubble& bubble: graph.bubbles) {
            const int64_t branchId0 = bubble.branchTable[sequenceId0];
            if(branchId0 < 0) {
                continue;
            }
            const int64_t branchId1 = bubble.branchTable[sequenceId1];
            if(branchId1 < 0) {
                continue;
            }
            if(branchId0 == branchId1) {
                ++sameCount;
            } else {
                ++differentCount;
            }
        }
        const uint64_t totalCount = sameCount + differentCount;
        bubbleCsv <<
            sequenceId1 << "," <<
            orientedReadIds[sequenceId1] << "," <<
            sameCount << "," <<
            differentCount << "," <<
            totalCount << "," <<
            double(sameCount)/double(totalCount) << "," <<
            double(differentCount)/double(totalCount) << "\n";
    }




    // Write out the marker graph in Graphviz format.
    ofstream graphOut("MiniAssembly-MarkerGraph.dot");
    graphOut <<
        "digraph MarkerGraph {\n"
        "tooltip = \" \";\n";

    // Vertices.
    BGL_FORALL_VERTICES_T(v, graph, Graph) {
        const auto& vertex = graph[v];
        const uint64_t coverage = vertex.coverage();
        graphOut << graph[v].vertexId << "[";
        graphOut << "width=" << 0.05 * sqrt(double(coverage));
        if(vertex.contains(sequenceId0)) {
            graphOut << " color=blue";
        }
        graphOut << " tooltip=\"" << coverage << "\"";
        graphOut << "];\n";
    }

    // Edges.
    BGL_FORALL_EDGES_T(e, graph, Graph) {
        const auto& edge = graph[e];
        const vertex_descriptor v0 = source(e, graph);
        const vertex_descriptor v1 = target(e, graph);
        const uint64_t coverage = edge.coverage();
        graphOut << graph[v0].vertexId << "->" <<
            graph[v1].vertexId << "[";
        graphOut << "penwidth=" << 1. * sqrt(double(coverage));
        if(edge.contains(sequenceId0)) {
            graphOut << " color=blue";
        }
        graphOut << " tooltip=\"" << coverage << "\"";
        graphOut << "];\n";
    }
    graphOut << "}\n";

}
