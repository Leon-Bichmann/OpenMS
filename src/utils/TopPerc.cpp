// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2016.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Mathias Walzer $
// $Authors: Andreas Simon, Mathias Walzer $
// --------------------------------------------------------------------------

#include <OpenMS/config.h>

#include <OpenMS/FORMAT/FileHandler.h>
#include <OpenMS/FORMAT/FileTypes.h>
#include <OpenMS/DATASTRUCTURES/StringListUtils.h>
#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/FORMAT/MzIdentMLFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/CONCEPT/ProgressLogger.h>
#include <OpenMS/CONCEPT/Constants.h>
#include <OpenMS/FORMAT/CsvFile.h>
#include <OpenMS/ANALYSIS/ID/TopPerc.h>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QProcess>

#include <iostream>
#include <cmath>
#include <string>

using namespace OpenMS;
using namespace std;

//-------------------------------------------------------------
//Doxygen docu
//-------------------------------------------------------------

/**
  @page TOPP_TopPerc TopPerc

  @brief TopPerc facilitates the input to, the call of and output integration of Percolator.
  Percolator (http://per-colator.com/) is a tool to apply semi-supervised learning for peptide
  identification from shotgun proteomics datasets.

  @experimental This tool is work in progress and usage and input requirements might change.

  <center>
  <table>
  <tr>
  <td ALIGN = "center" BGCOLOR="#EBEBEB"> potential predecessor tools </td>
  <td VALIGN="middle" ROWSPAN=3> \f$ \longrightarrow \f$ MSGF+\f$ \longrightarrow \f$</td>
  <td ALIGN = "center" BGCOLOR="#EBEBEB"> potential successor tools </td>
  </tr>
  <tr>
  <td VALIGN="middle" ALIGN ="center" ROWSPAN=1> @ref TOPP_IDFilter</td>
  <td VALIGN="middle" ALIGN ="center" ROWSPAN=1> @ref TOPP_IDMapper</td>
  </tr>
  </table>
  </center>

  <p>Percolator is search engine sensitive, i.e. it's input features vary, depending on the search engine.</p>

  <B>The command line parameters of this tool are:</B>
  @verbinclude TOPP_TopPerc.cli
  <B>INI file documentation of this tool:</B>
  @htmlinclude TOPP_TopPerc.html

  Percolator is written by Lukas Käll (http://per-colator.com/ Copyright Lukas Käll <lukas.kall@scilifelab.se>)
*/

// We do not want this class to show up in the docu:
/// @cond TOPPCLASSES


class TOPPPercolator :
  public TOPPBase
{
public:
  TOPPPercolator() :
    TOPPBase("TopPerc", "Facilitate input to Percolator and reintegrate.", false)
  {
  }

protected:
  void registerOptionsAndFlags_()
  {
    registerInputFile_("percolator_executable", "<executable>", "", "Path to the percolator binary", true, false, ListUtils::create<String>("skipexists"));
    registerInputFile_("in", "<file>", "", "Input target file", true);
    setValidFormats_("in", ListUtils::create<String>("mzid"));
    registerInputFile_("in_decoy", "<file>", "", "Input decoy file", false);
    setValidFormats_("in_decoy", ListUtils::create<String>("mzid"));
    registerOutputFile_("out", "<file>", "", "Output file", true);
    std::string enzs = "no_enzyme,elastase,pepsin,proteinasek,thermolysin,chymotrypsin,lys-n,lys-c,arg-c,asp-n,glu-c,trypsin";
    registerStringOption_("enzyme", "<enzyme>", "trypsin", "Type of enzyme: "+enzs , false, true);
    setValidStrings_("enzyme", ListUtils::create<String>(enzs));
    registerInputFile_("percolator_executable", "<executable>",
        // choose the default value according to the platform where it will be executed
        #ifdef OPENMS_WINDOWSPLATFORM
                       "percolator.exe",
        #else
                       "percolator",
        #endif
                       "Percolator executable of the installation e.g. 'percolator.exe'", true, false, ListUtils::create<String>("skipexists")
    );

    //Advanced parameters
//  //registerOutputFile_("r", "<file>", "out", "Output tab delimited results to a file instead of stdout", false, true);
    registerOutputFile_("X", "<file>", "", "path to file in xml-output format (pout). Default is: pout.tab", false, true);
    registerFlag_("e", "read xml-input format (pin) from standard input", true);
    registerFlag_("Z", "Include decoys (PSMs, peptides and/or proteins) in the xml-output. Only available if -X is used.", true);
    registerDoubleOption_("p", "<value>", 0.0, "Cpos, penalty for mistakes made on positive examples. Set by cross validation if not specified.", false, true);
    registerDoubleOption_("n", "<value>", 0.0, "Cneg, penalty for mistakes made on negative examples. Set by cross validation if not specified.", false, true);
    registerDoubleOption_("F", "<value>", 0.01, "False discovery rate threshold to define positive examples in training. Set by cross validation if 0. Default is 0.01.", false, true);
    registerDoubleOption_("t", "<value>", 0.01, "False discovery rate threshold for evaluating best cross validation result and the reported end result. Default is 0.01.", false, true);
    registerIntOption_("i", "<number>", 0, "Maximal number of iterations", false, true);
    registerFlag_("x", "Quicker execution by reduced internal cross-validation.", true);
    registerDoubleOption_("f", "<value>", 0.6, "Fraction of the negative data set to be used as train set when only providing one negative set, remaining examples will be used as test set. Set to 0.6 by default.", false, true);
    registerOutputFile_("J", "<file>", "", "Output the computed features to the given file in tab-delimited format. A file with the features with the given file name will be created", false, true);
    registerInputFile_("k", "<file>", "", "Input file given in the deprecated pin-xml format generated by e.g. sqt2pin with the -k option", false, true);
    registerOutputFile_("w", "<file>", "", "Output final weights to the given file", false, true);
    registerInputFile_("W", "<file>", "", "Read initial weights to the given file", false, true);
    registerStringOption_("V", "<featurename>", "", "The most informative feature given as the feature name, can be negated to indicate that a lower value is better.", false, true);
    registerIntOption_("v", "<level>", 2, "Set verbosity of output: 0=no processing info, 5=all, default is 2", false, true);
    registerFlag_("u", "Use unit normalization [0-1] instead of standard deviation normalization", true);
    registerFlag_("R", "Measure performance on test set each iteration", true);
    registerFlag_("O", "Override error check and do not fall back on default score vector in case of suspect score vector", true);
    registerIntOption_("S", "<value>", 1, "Setting seed of the random number generator. Default value is 1", false, true);
    registerFlag_("K", "Retention time features calculated as in Klammer et al.", true);
    registerFlag_("D", "Include description of correct features", true);
    registerOutputFile_("B", "<file>", "", "Output tab delimited results for decoys into a file", false, true);
    registerFlag_("U", "Do not remove redundant peptides, keep all PSMS and exclude peptide level probabilities.", true);
    registerFlag_("s", "skip validation of input file against xml schema", true);
    registerFlag_("A", "output protein level probabilities", true);
    registerDoubleOption_("a", "<value>", 0.0, "Probability with which a present protein emits an associated peptide (to be used jointly with the -A option). Set by grid search if not specified.", false, true);
    registerDoubleOption_("b", "<value>", 0.0, "Probability of the creation of a peptide from noise (to be used jointly with the -A option). Set by grid search if not specified", false, true);
    registerDoubleOption_("G", "<value>", 0.0, "Prior probability of that a protein is present in the sample ( to be used with the -A option). Set by grid search if not specified", false, true);
    registerFlag_("g", "treat ties as if it were one protein (Only valid if option -A is active).", true);
    registerFlag_("I", "use pi_0 value when calculating empirical q-values (no effect if option Q is activated) (Only valid if option -A is active).", true);
    registerFlag_("q", "output empirical q-values and p-values (from target-decoy analysis) (Only valid if option -A is active).", true);
    registerFlag_("N", "disactivates the grouping of proteins with similar connectivity, for example if proteins P1 and P2 have the same peptides matching both of them, P1 and P2 will not be grouped as one protein (Only valid if option -A is active).", true);
    registerFlag_("E", "Proteins graph will not be separated in sub-graphs (Only valid if option -A is active).", true);
    registerFlag_("C", "it does not prune peptides with a very low score (~0.0) which means that if a peptide with a very low score is matching two proteins, when we prune the peptide,it will be duplicated to generate two new protein groups (Only valid if option -A is active).", true);
    registerIntOption_("d", "<value>", 0, "Setting depth 0 or 1 or 2 from low depth to high depth(less computational time) of the grid search for the estimation Alpha,Beta and Gamma parameters for fido(Only valid if option -A is active). Default value is 0", false, true);
    registerStringOption_("P", "<value>", "random", "Define the text pattern to identify the decoy proteins and/or PSMs, set this up if the label that identifies the decoys in the database is not the default (by default : random) (Only valid if option -A  is active).", false, true);
    registerFlag_("T", "Reduce the tree of proteins (removing low scored proteins) in order to estimate alpha,beta and gamma faster.(Only valid if option -A is active).", true);
    registerFlag_("Y", "Use target decoy competition to compute peptide probabilities.(recommended when using -A).", true);
    registerFlag_("H", "Q-value threshold that will be used in the computation of the MSE and ROC AUC score in the grid search (recommended 0.05 for normal size datasets and 0.1 for big size datasets).(Only valid if option -A is active).", true);
    registerFlag_("fido-truncation", "Proteins with a very low score (< 0.001) will be truncated (assigned 0.0 probability).(Only valid if option -A is active)", true);
    registerFlag_("Q", "Uses protein group level inference, each cluster of proteins is either present or not, therefore when grouping proteins discard all possible combinations for each group.(Only valid if option -A is active and -N is inactive).", true);
    registerFlag_("MHC", "Add a feature for MHC ligand properties to the specific PSM.", true);
  }

  ExitCodes main_(int, const char**)
  {
    //-------------------------------------------------------------
    // general variables and data to perform TopPerc
    //-------------------------------------------------------------
    vector<PeptideIdentification> peptide_ids;
    vector<ProteinIdentification> protein_ids;

    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    const String in = getStringOption_("in");
    const String in_decoy = getStringOption_("in_decoy");
    writeDebug_(String("Input file of target: ") + in + " " + in_decoy, 2);

    const String percolator_executable(getStringOption_("percolator_executable"));
    writeDebug_(String("Path to the percolator: ") + percolator_executable, 2);
    if (percolator_executable.empty()) //TODO? - TOPPBase::findExecutable after registerInputFile_("percolator_executable"... ???
    {
      writeLog_("No percolator executable specified. Aborting!");
      printUsage_();
      return ILLEGAL_PARAMETERS;
    }

    //-------------------------------------------------------------
    // read input
    //-------------------------------------------------------------
    FileHandler fh;
    FileTypes::Type in_type = fh.getType(in);
    if (in_type == FileTypes::IDXML)
    {
      IdXMLFile().load(in, protein_ids, peptide_ids);
    }
    else if (in_type == FileTypes::MZIDENTML)
    {
      LOG_WARN << "Converting from mzid: you might experience loss of information depending on the capabilities of the target format." << endl;
      MzIdentMLFile().load(in, protein_ids, peptide_ids);
    }
    //else catched by TOPPBase:registerInput being mandatory mzid or idxml

    if (peptide_ids.empty())
    {
      writeLog_("No or empty input file specified. Aborting!");
      printUsage_();
      return ILLEGAL_PARAMETERS;
    }

    //being paranoid about the presence of target decoy denominations, which are crucial to the percolator process
    for (std::vector<PeptideIdentification>::iterator pit = peptide_ids.begin(); pit != peptide_ids.end(); ++pit)
    {
      for (vector<PeptideHit>::iterator pht = pit->getHits().begin(); pht != pit->getHits().end(); ++pht)
      {
        // Some Hits have no NumMatchedMainIons, and MeanError, etc. values. Have to ignore them!
        if (!pht->metaValueExists("target_decoy"))
        {
          if (!in_decoy.empty())
          {
            pht->setMetaValue("target_decoy", "target");
          }
          else
          {
            writeLog_("No target decoy search results discrimination possible. Aborting!");
            printUsage_();
            return ILLEGAL_PARAMETERS;
          }
        }
      }
    }

    //-------------------------------------------------------------
    // read more input if necessary
    //-------------------------------------------------------------
    //TODO check if this comes from the same search engine!
    if (!in_decoy.empty())
    {
      vector<PeptideIdentification> decoy_peptide_ids;
      vector<ProteinIdentification> decoy_protein_ids;
      FileTypes::Type in_decoy_type = fh.getType(in_decoy);
      if (in_decoy_type == FileTypes::IDXML)
      {
        IdXMLFile().load(in_decoy, decoy_protein_ids, decoy_peptide_ids);
      }
      else if (in_decoy_type == FileTypes::MZIDENTML)
      {
        LOG_WARN << "Converting from mzid: you might experience loss of information depending on the capabilities of the target format." << endl;
        MzIdentMLFile().load(in_decoy, decoy_protein_ids, decoy_peptide_ids);
      }
      for (std::vector<PeptideIdentification>::iterator pit = decoy_peptide_ids.begin(); pit != decoy_peptide_ids.end(); ++pit)
      {
        for (std::vector<PeptideHit>::iterator pht = pit->getHits().begin(); pht != pit->getHits().end(); ++pht)
        {
          pht->setMetaValue("target_decoy", "decoy");
          //TODO what about proteins - internal target decoy handling is shitty - rework pls
        }
      }
      //TODO this is going to fail with specrum_reference clashes if not handled _REALLY_ carefully
      peptide_ids.insert( peptide_ids.end(), decoy_peptide_ids.begin(), decoy_peptide_ids.end() );
      protein_ids.insert( protein_ids.end(), decoy_protein_ids.begin(), decoy_protein_ids.end() );
      writeLog_("Using decoy hits from separate file.");
    }
    else
    {
      writeLog_("Using decoy hits from input id file. You did you use a target decoy search, did you?");
//        printUsage_();
//        return ILLEGAL_PARAMETERS;
    }


    //-------------------------------------------------------------
    // extract search engine and prepare pin
    //-------------------------------------------------------------
    String se = protein_ids.front().getSearchEngine();
    TextFile txt;

    //TODO introduce min/max charge to parameters for now take available range
    int maxCharge = 0;
    int minCharge = 10;
    for (vector<PeptideIdentification>::iterator it = peptide_ids.begin(); it != peptide_ids.end(); ++it)
    {
      for (vector<PeptideHit>::const_iterator hit = it->getHits().begin(); hit != it->getHits().end(); ++hit)
      {
        if (hit->getCharge() > maxCharge)
        {
          maxCharge = hit->getCharge();
        }
        if (hit->getCharge() < minCharge)
        {
          minCharge = hit->getCharge();
        }
      }
    }

    std::string enz_str = getStringOption_("enzyme");
    writeDebug_("Detected search engine: " + se , 2);
    if (se == "MS-GF+") TopPerc::prepareMSGFpin(peptide_ids, enz_str, txt, minCharge, maxCharge, getFlag_("MHC"));
//    if (se == "Mascot") prepareMASCOTpin(peptide_ids, txt, minCharge, maxCharge);
    if (se == "XTandem") TopPerc::prepareXTANDEMpin(peptide_ids, enz_str, txt, minCharge, maxCharge);

    writeLog_( "Executing percolator!");

    // create temp directory to store percolator in file pin.tab temporarily
    String temp_directory_body = QDir::toNativeSeparators((File::getTempDirectory() + "/" + File::getUniqueName() + "/").toQString()); // body for the tmp files
    {
      QDir d;
      d.mkpath(temp_directory_body.toQString());
    }

    String txt_designator = File::getUniqueName();
    String pin_file(temp_directory_body + txt_designator + "_pin.tab");
    String pout_file(temp_directory_body + txt_designator + "_pout.tab");

    // File is stored in temp directory
    txt.store(pin_file);

    QStringList arguments;
    // Check all set parameters and get them into arguments StringList
    arguments << "-r" << pout_file.toQString();
    if (getFlag_("e")) arguments << "-e";
    if (getFlag_("Z")) arguments << "-Z";
    if (getDoubleOption_("p") != 0.0) arguments << "-p" << String(getDoubleOption_("p")).toQString();
    if (getDoubleOption_("n") != 0.0) arguments << "-n" << String(getDoubleOption_("n")).toQString();
    if (getDoubleOption_("F") != 0.01) arguments << "-F" << String(getDoubleOption_("F")).toQString();
    if (getDoubleOption_("t") != 0.01) arguments << "-t" << String(getDoubleOption_("t")).toQString();
    if (getIntOption_("i") != 0) arguments << "-i" << String(getIntOption_("i")).toQString();
    if (getFlag_("x")) arguments << "-x";
    if (getDoubleOption_("f") != 0.6) arguments << "-f" << String(getDoubleOption_("f")).toQString();
    if (getStringOption_("J") != "") arguments << "-J" << getStringOption_("J").toQString();
    if (getStringOption_("k") != "") arguments << "-k" << getStringOption_("k").toQString();
    if (getStringOption_("w") != "") arguments << "-w" << getStringOption_("w").toQString();
    if (getStringOption_("W") != "") arguments << "-W" << getStringOption_("W").toQString();
    if (getStringOption_("V") != "") arguments << "-V" << getStringOption_("V").toQString();
    if (getIntOption_("v") != 2) arguments << "-v" << String(getIntOption_("v")).toQString();
    if (getFlag_("u")) arguments << "-u";
    if (getFlag_("R")) arguments << "-R";
    if (getFlag_("O")) arguments << "-O";
    if (getIntOption_("S") != 1) arguments << "-S" << String(getDoubleOption_("S")).toQString();
    if (getFlag_("K")) arguments << "-K";
    if (getFlag_("D")) arguments << "-D";
    if (getStringOption_("B") != "") arguments << "-B" << getStringOption_("B").toQString();
    if (getFlag_("U")) arguments << "-U";
    if (getFlag_("s")) arguments << "-s";
    if (getFlag_("A")) arguments << "-A";
    if (getDoubleOption_("a") != 0.0) arguments << "-a" << String(getDoubleOption_("a")).toQString();
    if (getDoubleOption_("b") != 0.0) arguments << "-b" << String(getDoubleOption_("b")).toQString();
    if (getDoubleOption_("G") != 0.0) arguments << "-G" << String(getDoubleOption_("G")).toQString();
    if (getFlag_("g")) arguments << "-g";
    if (getFlag_("I")) arguments << "-I";
    if (getFlag_("q")) arguments << "-q";
    if (getFlag_("N")) arguments << "-N";
    if (getFlag_("E")) arguments << "-E";
    if (getFlag_("C")) arguments << "-C";
    if (getIntOption_("d") != 0) arguments << "-d" << String(getIntOption_("d")).toQString();
    if (getStringOption_("P") != "random") arguments << "-P" << getStringOption_("P").toQString();
    if (getFlag_("T")) arguments << "-T";
    if (getFlag_("Y")) arguments << "-Y";
    if (getFlag_("H")) arguments << "-H";
    if (getFlag_("fido-truncation")) arguments << "--fido-truncation";
    if (getFlag_("Q")) arguments << "-Q";
    arguments << "-U";
    arguments << pin_file.toQString();

    // Percolator execution with the executable ant the arguments StringList
    int status = QProcess::execute(percolator_executable.toQString(), arguments); // does automatic escaping etc...
    if (status != 0)
    {
      writeLog_("Percolator problem. Aborting! Calling command was: '" + percolator_executable + " \"" + arguments.join("-").toStdString() + "\".");
      // clean temporary files
      if (this->debug_level_ < 2)
      {
        File::removeDirRecursively(temp_directory_body);
        LOG_WARN << "Set debug level to >=2 to keep the temporary files at '" << temp_directory_body << "'" << std::endl;
      }
      else
      {
        LOG_WARN << "Keeping the temporary files at '" << temp_directory_body << "'. Set debug level to <2 to remove them." << std::endl;
      }
      return EXTERNAL_PROGRAM_ERROR;
    }

    // when percolator finished calculation, it stores the results -r option (with or without -U) or -m (which seems to be not working)
    CsvFile csv_file(pout_file, '\t');

    map<String, vector<String> > pep_map;
    StringList row;

    for (size_t i = 1; i < csv_file.rowCount(); ++i)
    {
      csv_file.getRow(i, row);
      vector<String> row_values;
      // peptide
      row_values.push_back(row[4].chop(2).reverse().chop(2).reverse());
//      writeDebug_("sequence: " + row[4].chop(2).reverse().chop(2).reverse(), 99);
      // SVM-score
      row_values.push_back(row[1]);
      // q-Value
      row_values.push_back(row[2]);
      // PEP
      row_values.push_back(row[3]);

      vector<String> substr;
      row[0].split('_', substr);
//      writeDebug_("Mapping input to key: " + substr[2] , 2);
      pep_map[substr[2]] = row_values; // scannr. as written in preparePIN
    }

    // As the percolator output file is not needed anymore, the temporary directory is going to be deleted
    if (this->debug_level_ < 99)
    {
      File::removeDirRecursively(temp_directory_body);
    }
    else
    {
      LOG_WARN << "Keeping the temporary files at '" << temp_directory_body << "'. Set debug level to <2 to remove them." << std::endl;
    }

    // Add the percolator results to the peptide vector of the original input file
    size_t c_debug = 0;
    for (vector<PeptideIdentification>::iterator it = peptide_ids.begin(); it != peptide_ids.end(); ++it)
    {
      String sid = it->getMetaValue("spectrum_reference");
      if (pep_map.find(sid) == pep_map.end())
      {
        //writeDebug_("No suitable PeptideIdentification entry 1st found for " + sid  , 2);
        vector<String> sr;
        sid.split('=', sr);
        sid = sr.back();
        if (pep_map.find(sid) == pep_map.end())
        {
          //writeDebug_("No suitable PeptideIdentification entry 2nd found for " + sid + " - emulate percolator scores with exisiting scores?", 111);
          ++c_debug;
          writeDebug_("No suitable PeptideIdentification entry for " + sid, 3);
          continue;
        }
      }

      it->setScoreType("q-value");
      it->setHigherScoreBetter(false);
      AASequence aat;
//      writeDebug_("sequence: " + pep_map[sid][0], 99);
      aat = AASequence::fromString(pep_map[sid][0]);
//      writeDebug_("sequence: " + aat.toString(), 99);

      for (vector<PeptideHit>::iterator hit = it->getHits().begin(); hit != it->getHits().end(); ++hit)
      {
        if (hit->getSequence() == aat)
        {
          //get aa before/after/charge and metainfo
          hit->setMetaValue("MS:1001492", pep_map[sid][1].toDouble());        //svm score
          double qv = pep_map[sid][2].toDouble();        // q-value
          hit->setMetaValue("MS:1001491", qv);
          hit->setMetaValue("prepercolatorscore", hit->getScore());
          writeDebug_("found peptide and wrote percolator scoring from "+String(hit->getScore())+" to "+String(qv), 99);
          hit->setScore(qv);
          hit->setMetaValue("MS:1001493", pep_map[sid][3].toDouble());        //pep
          //writeDebug_("found peptide and wrote percolator scoring", 99);
        }
        else writeDebug_(aat.toString()+" - found nothing and wrote no percolator scoring", 99);
      }
    }
    writeDebug_("No suitable PeptideIdentification for " + String(c_debug) + " out of " + String(peptide_ids.size()), 2);

    for (vector<ProteinIdentification>::iterator it = protein_ids.begin(); it != protein_ids.end(); ++it)
    {
      it->setSearchEngine("Percolator");
      //TODO add software percolator and topperc
    }

    // Storing the PeptideHits with calculated q-value, pep and svm score
    MzIdentMLFile().store(getStringOption_("out").toQString().toStdString(), protein_ids, peptide_ids);

    LOG_INFO << "TopPerc finished successfully!" << endl;

    return EXECUTION_OK;
  }

};


int main(int argc, const char** argv)
{
  TOPPPercolator tool;

  return tool.main(argc, argv);
}

/// @endcond
