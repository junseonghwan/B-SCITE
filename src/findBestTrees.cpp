/*
 * findBestTrees_noR.cpp
 *
 *  Created on: Mar 27, 2015
 *      Author: jahnka
 */

#include <stdbool.h>
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include "matrices.h"
#include "treelist.h"
#include "trees.h"
#include "output.h"
#include "mcmc.h"
#include "rand.h"
#include "scoreTree.h"
#include "bulkScoring.h"
#include "doublets.h"

using namespace std;

int** getDataMatrix(int n, int m, string fileName);
Mutation* readMutationsFromBulkFile(string pathBulkFile, int numMutations); // numMutations = n
int*  getNumCellsMutPresentArray(int n, int m, string fileName);
double* getErrorRatesArray(double fd, double ad1, double ad2, double cc);
int readParameters(int argc, char* argv[]);
string getOutputFilePrefix(string fileName, string outFilenamePrefix);
string getFileName(string prefix, string ending);
string getFileName2(int i, string prefix, string ending, char scoreType);
vector<string> getGeneNames(string fileName, int nOrig);
vector<double> setMoveProbs();
int* getParentVectorFromGVfile(string fileName, int n);
int getMinDist(int* trueVector, std::vector<bool**> optimalTrees, int n);
void printGeneFrequencies(int** dataMatrix, int n, int m, vector<string> geneNames);
string getFullCommand(int argc, char* argv[]);

double defaultMoveProbs[] = {0.55, 0.4, 0.05};     // moves: change beta / prune&re-attach / swap node labels / swap subtrees
double defaultMoveProbsBin[] = {0.4, 0.6};    // moves: change beta / prune&re-attach / swap leaf labels

double w = 0;
double errorRateMove = 0.0;
vector<double> treeMoves;
double chi = 10;
double priorSd = 0.1;
string fileName;      // data file
string bulkFileLocation;
string outFilenamePrefix;       // the name of the outputfile, only the prefix before the dot
int n;                // number of genes
int m;                // number of samples
char scoreType = 'm';
int rep;            // number of repetitions of the MCMC
int loops;          // number of loops within a MCMC
double gammaParam = 1;
double fd;          // rate of false discoveries (false positives 0->1)
double ad1;          // rate of allelic dropout (false negatives 1->0)
double doubletProb;
int doubleMut = -1;
double ad2 = 0.0;         // rate of allelic dropout (2->1)
double cc = 0.0;          // rate of falsely discovered homozygous mutations (0->2)
bool sample = false;
int sampleStep;
bool useGeneNames = false;        // use gene names in tree plotting
string geneNameFile;              // file where the gene names are listed.
bool trueTreeComp = false;      // set to true if true tree is given as parameter for comparison
string trueTreeFileName;        // optional true tree
bool attachSamples = false;       // attach samples to the tree
bool useFixedSeed = false;      // use a predefined seed for the random number generator
unsigned int fixedSeed = 1;   // default seed
bool useTreeList = true;
char treeType = 'm';        // the default tree is a mutation tree; other option is 't' for (transposed case), where we have a binary leaf-labeled tree
int maxTreeListSize = -1;  // defines the maximum size of the list of optimal trees, default -1 means no restriction
double *optimalAlpha, *optimalBeta;
double doublet_alphaBetaMoveRatio = -1; // set default value to 0.50 or 1.00 later (this is percentage of alpha, 0.00 means no alpha learning)
int main(int argc, char* argv[])
{
	cout << getFullCommand(argc, argv) << endl;

	/****************   begin timing  *********************/
			clock_t begin=clock();
	/****************************************************/
	
	std::vector<struct treeBeta> optimalTrees;            // list of optimal tree/beta combinations found by MCMC
	std::string sampleOutput;                            // the samples taken in the MCMC as a string for outputting



	/**  read parameters and data file  **/
	readParameters(argc, argv);
	int** dataMatrix = getDataMatrix(n, m, fileName); // see description of getDataMatrix below - before source code of the function Output is of dimensions m x n
	int* numCellsMutPresent = getNumCellsMutPresentArray(n, m, fileName);
	Mutation* bulkMutations = readMutationsFromBulkFile(bulkFileLocation, n);
	vector<double> moveProbs = setMoveProbs();
	double* errorRates = getErrorRatesArray(fd, ad1, ad2, cc);

	/* initialize the random number generator, either with a user defined seed, or a random number */
		useFixedSeed? srand(fixedSeed) : initRand();

	/** get the true parent vector from GraphViz file if available (for simulated data only)  **/
	int* trueParentVec = NULL;
	if(trueTreeComp==true){ trueParentVec = getParentVectorFromGVfile(trueTreeFileName, n); }

	/**  Find best scoring trees by MCMC  **/
	optimalAlpha = new double[n];
	optimalBeta  = new double[n];
//	sampleOutput = runMCMCbeta(optimalTrees, errorRates, rep, loops, gammaParam, moveProbs, n, m, dataMatrix, scoreType, trueParentVec, sampleStep, sample, chi, priorSd, useTreeList, treeType, bulkMutations, w, outFilenamePrefix, optimalAlpha, optimalBeta);

	//doublet Prob, doubleMut, priorSd_alpha, priorSd_beta
	double priorSd_alpha = 0.01* priorSd;
	double priorSd_beta  = priorSd;
	sampleOutput = runMCMCbetaDoublet(optimalTrees, errorRates, rep, loops, gammaParam, moveProbs, n, m, dataMatrix, scoreType, trueParentVec, sampleStep, sample, chi, priorSd_beta, priorSd_alpha, doublet_alphaBetaMoveRatio, useTreeList, treeType, doubletProb, doubleMut, bulkMutations, w, outFilenamePrefix);
	/***  output results  ***/

	string prefix = getOutputFilePrefix(fileName, outFilenamePrefix);

	/* output the samples taken in the MCMC */
	stringstream sampleOutputFile;
	sampleOutputFile << prefix << ".samples";
	writeToFile(sampleOutput, sampleOutputFile.str());
	cout << "samples from posterior written to: " << sampleOutputFile.str() << "\n";

	/* output the optimal trees found in individual files */

	double** logScores = getLogScores(fd, ad1, ad2, cc);
	int parentVectorSize = n;
	if(treeType=='t'){parentVectorSize = (2*m)-2;}                     // transposed case: binary tree, m leafs and m-1 inner nodes, root has no parent
	int outputSize = optimalTrees.size();
	if(maxTreeListSize >=0) {outputSize = maxTreeListSize;}            // there is a limit on the number of trees to output

/*
	for(int i=0; i<outputSize; i++){

		int* parentVector = optimalTrees.at(i).tree;
		bool** ancMatrix = parentVector2ancMatrix(parentVector, parentVectorSize);
		vector<vector<int> > childLists = getChildListFromParentVector(parentVector, parentVectorSize);

		stringstream newick;
		string outputFile = getFileName2(i, prefix, ".newick", scoreType);
		newick << getNewickCode(childLists, parentVectorSize) << "\n";
		writeToFile(newick.str(), outputFile);
		outputFile = getFileName2(i, prefix, ".gv", scoreType);	                                // print out tree as newick code

		if(errorRateMove != 0.0){
			updateLogScores(logScores, optimalTrees[i].beta);
		}

		if(treeType == 'm'){
			string output;
			output = getGraphVizFileContentNames(parentVector, parentVectorSize, getGeneNames(geneNameFile, n), attachSamples, ancMatrix, m, logScores, dataMatrix);
			writeToFile(output, outputFile);
		}
		else{
			int* bestPlacement = getHighestOptPlacementVector(dataMatrix, n, m, logScores, ancMatrix);
			vector<string> names = getGeneNames(geneNameFile, n);
			vector<string> bestBinTreeLabels = getBinTreeNodeLabels((2*m)-1, bestPlacement, n, getGeneNames(geneNameFile, n));
			//cout << getGraphVizBinTree(optimalTrees.at(0).tree, (2*m)-1, m, bestBinTreeLabels);
		}

		free_boolMatrix(ancMatrix);

	}

*/
	delete [] logScores[0];
	delete [] logScores;
	delete [] errorRates;
	free_intMatrix(dataMatrix);
	cout << optimalTrees.size() << " opt trees \n";
	emptyVectorFast(optimalTrees, n);


	/****************   end timing  *********************/
  		clock_t end=clock();
  		double diffticks=end-begin;
  		double diffms=(diffticks*1000)/CLOCKS_PER_SEC;
  		cout << "Time elapsed: " << diffms << " ms"<< endl;
		double totalTimeHours = diffms/(1000.0 * 3600);
		cout << "REPS: "  << rep << endl;
		cout << "LOOPS: " << loops << endl;
		cout << "TIME_PER_SINGLE_MCMC_STEP_HOURS: " << totalTimeHours/(rep*loops) << endl;
		cout << "TIME_PER_SINGLE_MCMC_STEP_SECS:  " << (totalTimeHours*3600)/(rep*loops) << endl;
		cout << "TOTAL_RUNNING_TIME_HOURS: " << totalTimeHours << endl;

  	/****************************************************/
}




void printGeneFrequencies(int** dataMatrix, int n, int m, vector<string> geneNames){
	for(int i=0; i<n; i++){
		int freq = 0;
		for(int j=0; j<m; j++){
			if(dataMatrix[j][i]==1 || dataMatrix[j][i]==2){
				freq++;
			}
		}
		cout << freq << "\t" << geneNames.at(i) << "\n";
	}
}

int* getParentVectorFromGVfile(string fileName, int n){
	int* parentVector = new int[n];
	std::vector<std::string> lines;
	std::ifstream file(fileName.c_str());
	std::string line;
	while ( std::getline(file, line) ) {
	    if ( !line.empty() )
	        lines.push_back(line);
	}
	for(int i=0; i < lines.size(); i++){

		std::size_t found = lines[i].find(" -> ");
		if (found!=std::string::npos){
			int parent = atoi(lines[i].substr(0, found).c_str());
			int child = atoi(lines[i].substr(found+3).c_str());
			parentVector[child-1] = parent-1;
	   }
	}
	return parentVector;
}



int getMinDist(int* trueVector, std::vector<bool**> optimalTrees, int n){
	int minDist = n+1;
	for(int i=0; i<optimalTrees.size(); i++){
		int dist = getSimpleDistance(trueVector, ancMatrixToParVector(optimalTrees.at(i), n), n);
		minDist = min(minDist, dist);
	}
	return minDist;
}


string getOutputFilePrefix(string fileName, string outFilenamePrefix){
	if(outFilenamePrefix.empty()){
		int lastIndex = fileName.find_last_of(".");
		return fileName.substr(0, lastIndex);
	}
	return outFilenamePrefix;
}


string getFileName(string prefix, string ending){
	stringstream fileName;
	fileName << prefix << ending;
	return fileName.str();
}

string getFileName2(int i, string prefix, string ending, char scoreType){
	stringstream fileName;
	if(scoreType == 'm'){
		fileName << prefix << "_ml" << i << ending;
	}
	else{
		fileName << prefix << "_map" << i << ending;
	}
	return fileName.str();
}

int readParameters(int argc, char* argv[]){
	for (int i = 1; i < argc; ++i) {

		if (strcmp(argv[i], "-i") == 0) {
			if (i + 1 < argc) { fileName = argv[++i];}
		} else if (strcmp(argv[i], "-t") == 0) {
			if (i + 1 < argc) {
				trueTreeFileName = argv[++i];
				trueTreeComp = true;
			}
		} else if(strcmp(argv[i], "-o")==0) {
			if (i + 1 < argc) { outFilenamePrefix = argv[++i];}
		} else if(strcmp(argv[i], "-n")==0) {
			if (i + 1 < argc) { n = atoi(argv[++i]);}
		} else if(strcmp(argv[i], "-m")==0) {
			if (i + 1 < argc) { m = atoi(argv[++i]);}
		} else if(strcmp(argv[i], "-r") == 0) {
			if (i + 1 < argc) { rep = atoi(argv[++i]);}
		} else if(strcmp(argv[i], "-l")==0) {
			if (i + 1 < argc) { loops = atoi(argv[++i]);}
		} else if(strcmp(argv[i], "-g")==0) {
			if (i + 1 < argc) { gammaParam = atof(argv[++i]);}
		} else if(strcmp(argv[i], "-fd")==0) {
			if (i + 1 < argc) { fd = atof(argv[++i]);}
		} else if(strcmp(argv[i],"-ad")==0) {
			if (i + 1 < argc) { ad1 = atof(argv[++i]);}
			if (i + 1 < argc){
				string next = argv[i+1];
				if(next.compare(0, 1, "-") != 0){
					ad2 = atof(argv[++i]);
				}
			}
		} else if(strcmp(argv[i], "-dr")==0) {
			if (i+1 < argc) { doubletProb = atof(argv[++i]);}

		}	else if(strcmp(argv[i],"-cc")==0) {
			if (i + 1 < argc) { cc = atof(argv[++i]);}
		} else if(strcmp(argv[i],"-e")==0) {
					if (i + 1 < argc) { errorRateMove = atof(argv[++i]);}
		} else if(strcmp(argv[i],"-x")==0) {
							if (i + 1 < argc) { chi = atof(argv[++i]);}
		} else if(strcmp(argv[i],"-sd")==0) {
							if (i + 1 < argc) { priorSd = atof(argv[++i]);}
		} else if(strcmp(argv[i],"-abr")==0) {
							if (i + 1 < argc) { doublet_alphaBetaMoveRatio = atof(argv[++i]);}
		} else if (strcmp(argv[i], "-a")==0) {
			attachSamples = true;
		} else if(strcmp(argv[i], "-p")==0) {
			if (i + 1 < argc) {
				sampleStep = atoi(argv[++i]);
				sample = true;
			}
		}else if (strcmp(argv[i], "-names")==0) {
			useGeneNames = true;
			if (i + 1 < argc) { geneNameFile = argv[++i];}
		}else if (strcmp(argv[i], "-move_probs")==0) {
			vector<double> newMoveProbs;
			if (i + 1 < argc) { treeMoves.push_back(atof(argv[++i]));}
			if (i + 1 < argc) { treeMoves.push_back(atof(argv[++i]));}
			if (i + 1 < argc){
				string next = argv[i+1];
				if(next.compare(0, 1, "-") != 0){
					treeMoves.push_back(atof(argv[++i]));
				}
			}
			//cout << move1_prob << " " << move2_prob << " " << move3_prob << "\n";

		}else if (strcmp(argv[i], "-seed")==0) {
			useFixedSeed = true;
			if (i + 1 < argc) { fixedSeed = atoi(argv[++i]);}
		}else if (strcmp(argv[i], "-max_treelist_size")==0) {
					if (i + 1 < argc) { maxTreeListSize = atoi(argv[++i]);}
		} else if (strcmp(argv[i],"-no_tree_list")==0) {
					useTreeList = false;
		} else if (strcmp(argv[i],"-s")==0) {
			scoreType = 's';
		} else if (strcmp(argv[i],"-transpose")==0) {
					treeType = 't';
		} else if (strcmp(argv[i], "-bulkFileLocation") == 0) {
		 			bulkFileLocation = argv[++i];  
		} else if (strcmp(argv[i], "-w") == 0){
				w = atof(argv[++i]); 
		} else {
			std::cerr << "unknown parameter " << argv[i] << std::endl;
			return 1;
		}
	}
	return 0;
}


vector<double> setMoveProbs(){
	vector<double> moveProbs;

	moveProbs.push_back(errorRateMove);

	if(treeMoves.size()==0){                                       // use default probabilities
		if(treeType == 'm'){
			moveProbs.push_back(defaultMoveProbs[0]);
			moveProbs.push_back(defaultMoveProbs[1]);
			moveProbs.push_back(defaultMoveProbs[2]);
		}
		else{
			moveProbs.push_back(defaultMoveProbsBin[0]);
			moveProbs.push_back(defaultMoveProbsBin[1]);
		}
	}
	else{                                                                            // use probabilities from command line
		double sum = 0.0;
		for(int i=0; i< treeMoves.size(); i++){ sum += treeMoves[i]; }
		if(sum != 1.0){
			cerr << "move probabilities do not sum to 1.0, recalculating probabilities\n";     // normalize to sum to one
			for(int i=0; i< treeMoves.size(); i++){
				treeMoves[i] = treeMoves[i]/sum;
			}
			cout << "new move probabilities:";
			for(int i=0; i< treeMoves.size(); i++){ cout << " " << treeMoves[i];}
			cout << "\n";
		}
		for(int i=0; i< treeMoves.size(); i++){
			moveProbs.push_back(treeMoves[i]);
		}
	}
	treeMoves.clear();
	return moveProbs;
}


/*
	OUTPUT: m x n matrix (m -- num of cells, n num of samples)
	Expected data matrix should be of dimensions n X m , where n is number of mutations and m number of cells.
	In this matrix, each column corresponds to the mutation profile of single cell, and rows correspond to
	mutations.
	However, at the end of this function, the matrix that is initially storing the input file in matrix of 
	dimensions n x m  is transposed so the matrix resulting from this function is of dimensions m x n.
*/

int** getDataMatrix(int n, int m, string fileName){

    int** dataMatrix = init_intMatrix(n, m, -1);

    ifstream in(fileName.c_str());

    if (!in) {
    	cout << "2 Cannot open file " << fileName << "\n";
      cout << fileName;
      cout << "\n";
      return NULL;
    }

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            in >> dataMatrix[i][j];
        }
    }

    in.close();
    int** transposedMatrix = transposeMatrix(dataMatrix, n, m);
    free_intMatrix(dataMatrix);

    return transposedMatrix;
}

Mutation* readMutationsFromBulkFile(string pathBulkFile, int n){
	Mutation* bulkMutations = new Mutation[n];
	assertFileExists(pathBulkFile);
	ifstream bulkFile(pathBulkFile);
	string bulkFileHeader;
	getline(bulkFile, bulkFileHeader);
	string line;
	int currentMutationIndex = 0;
	while(getline(bulkFile, line)){
		/* read Entries of the lines (prepare value of each of the fields of Mutation object one by one)
		    example of input file line with three bulk samples (see line below):
		    mutID chr1 3242234 456;235;223; 1004;1315;1284; SomeINFO
		*/
		vector<string> lineColumns = splitString(line, "\t ");
		string ID = lineColumns[0];
		string chromosome = lineColumns[1];
		int position = atoi(lineColumns[2].c_str());
		vector<string> mutReadsVectorString = splitString(lineColumns[3], ";");
		vector<string> refReadsVectorString  = splitString(lineColumns[4], ";");
		if(mutReadsVectorString.size() != refReadsVectorString.size()){
			cout << "ERROR in file " << pathBulkFile << ". The number of mut and ref reads must be same. Line content: " << line << endl;
			assert(false);}
		int numSamples = mutReadsVectorString.size();
		vector<int> mutReads,refReads;
		for(int sampleIndex=0; sampleIndex<numSamples; sampleIndex++){
			mutReads.push_back(atoi(mutReadsVectorString[sampleIndex].c_str()));
			refReads.push_back(atoi(refReadsVectorString[sampleIndex].c_str()));
		}
		string INFO = lineColumns[5];
		bulkMutations[currentMutationIndex] = Mutation(ID, chromosome, position, mutReads, refReads, INFO);
		currentMutationIndex++;
	}
	// Do some validation of the input data
	int numMutations = currentMutationIndex;
	if(currentMutationIndex == 0){
		cout << "Bulk file " << pathBulkFile << " does not contain any mutation";
		assert(numMutations>0);
	}
	int numSamples = bulkMutations[0].getNumSamples();
	for(int i=1; i<numMutations; i++){
		if(bulkMutations[i].getNumSamples() != numSamples){
			cout << "Number of bulk samples must be the same for all mutations. Place zero for read count if mutation missing. File " << pathBulkFile << endl;
			assert(numSamples == bulkMutations[i].getNumSamples());
		}
	}
	return bulkMutations;
}



int* getNumCellsMutPresentArray(int n, int m, string fileName){
	cout << "Mutations absent from all cells:\t";
	int* numCellsMutPresent = new int[n];
	for(int i=0; i<n; i++)
		numCellsMutPresent[i] = 0;

	int a;
	ifstream in(fileName.c_str());

	if (!in) {
		cout << "2 Cannot open file " << fileName << "\n";
      		cout << fileName;
      		cout << "\n";
      		return NULL;
    	}

    	for (int i = 0; i < n; i++) {
        	for (int j = 0; j < m; j++) {
           		 //in >> dataMatrix[i][j];
			 in >> a;
			 numCellsMutPresent[i] += a;
       		}
		if(numCellsMutPresent[i] == 0)
			cout << "\t" << i;
    	}
	cout << endl;
	cout << "numCellsMutPresent:";
	for(int i=0; i<n; i++)
		cout << "\t" << numCellsMutPresent[i];
	cout << endl;

	in.close();
	
	return numCellsMutPresent;
}


vector<string> getGeneNames(string fileName, int nOrig){

	vector<string> v;
	ifstream in(fileName.c_str());


	n = nOrig;

	if (!in) {
		//cout << "Cannot open gene names file " << fileName << ", ";
	    //cout << "using ids instead.\n";
	    vector<string> empty;
	    for(int i=0; i<=n; i++){
	    	stringstream id;
	    	id << i+1;
	    	empty.push_back(id.str());
	    }
	    return empty;
	}

	for (int i = 0; i < nOrig; i++) {
		string temp;
	    in >> temp;
	    v.push_back(temp);
	}
	v.push_back("Root"); // the root
	return v;
}



double* getErrorRatesArray(double fd, double ad1, double ad2, double cc){
	double* array = new double[4];
	array[0] = fd;
	array[1] = ad1;
	array[2] = ad2;
	array[3] = cc;
	return array;
}


/*
	This function prints the full command used for running B-SCITE
*/
string getFullCommand(int argc, char* argv[]){
	string fullCommand = "FULL_COMMAND:";
	for(int i=0; i<argc; i++){
		fullCommand += "\t";
		fullCommand += argv[i];
	}
	return fullCommand;
}


