// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2015.
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
// $Authors: Andreas Simon, Mathias Walzer, Matthew The $
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
  @page TOPP_PSMFeatureExtractor PSMFeatureExtractor

  @brief PSMFeatureExtractor computes extra features for each input PSM

  @experimental This tool is work in progress and usage and input requirements might change.

  <center>
  <table>
  <tr>
  <td ALIGN = "center" BGCOLOR="#EBEBEB"> potential predecessor tools </td>
  <td VALIGN="middle" ROWSPAN=3> \f$ \longrightarrow \f$ MSGF+\f$ \longrightarrow \f$</td>
  <td ALIGN = "center" BGCOLOR="#EBEBEB"> potential successor tools </td>
  </tr>
  <tr>
  <td VALIGN="middle" ALIGN ="center" ROWSPAN=1> @ref TOPP_PercolatorAdapter</td>
  </tr>
  </table>
  </center>

  <p>PSMFeatureExtractor is search engine sensitive, i.e. it's extra features vary, depending on the search engine.</p>

  <B>The command line parameters of this tool are:</B>
  @verbinclude TOPP_PSMFeatureExtractor.cli
  <B>INI file documentation of this tool:</B>
  @htmlinclude TOPP_PSMFeatureExtractor.html
*/

// We do not want this class to show up in the docu:
/// @cond TOPPCLASSES


class PSMFeatureExtractor :
  public TOPPBase
{
public:
  PSMFeatureExtractor() :
    TOPPBase("PSMFeatureExtractor", "Computes extra features for each input PSM.", false)
  {
  }

protected:
  void registerOptionsAndFlags_()
  {
    registerInputFileList_("in", "<files>", StringList(), "Input file(s)", true);
    setValidFormats_("in", ListUtils::create<String>("mzid,idXML"));
    registerOutputFile_("out", "<file>", "", "Output file in idXML format", false);
    registerOutputFile_("mzid_out", "<file>", "", "Output file in mzid format", false);
    registerFlag_("multiple_search_engines", "Combine PSMs from different search engines by merging on scan level.");

    registerFlag_("MHC", "Add a feature for MHC ligand properties to the specific PSM.", true);
    registerFlag_("override_db_check", "Manual override to check if same settings for multiple search engines were applied.", true);
    registerFlag_("concat", "Naive merging of PSMs from different search engines: concatenate multiple search results instead of merging on scan level. Only valid together wtih -multiple_search_engines flag.", true);
  }
  
  ExitCodes main_(int, const char**)
  {
    //-------------------------------------------------------------
    // general variables and data to perform PSMFeatureExtractor
    //-------------------------------------------------------------
    vector<PeptideIdentification> all_peptide_ids;
    vector<ProteinIdentification> all_protein_ids;

    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    const StringList in_list = getStringList_("in");
    LOG_DEBUG << "Input file (of target?): " << ListUtils::concatenate(in_list, ",") << endl;

    const String mzid_out(getStringOption_("mzid_out"));
    const String out(getStringOption_("out"));
    if (mzid_out.empty() && out.empty())
    {
      writeLog_("Fatal error: no output file given (parameter 'out' or 'mzid_out')");
      printUsage_();
      return ILLEGAL_PARAMETERS;
    }

    //-------------------------------------------------------------
    // read input
    //-------------------------------------------------------------
    bool multiple_search_engines = getFlag_("multiple-search-engines");
    bool override_db_check = getFlag_("override_db_check");
    bool concatenate = getFlag_("concat");
    StringList search_engines_used;
    for (StringList::const_iterator fit = in_list.begin(); fit != in_list.end(); ++fit)
    {
      vector<PeptideIdentification> peptide_ids;
      vector<ProteinIdentification> protein_ids;
      String in = *fit;
      FileHandler fh;
      FileTypes::Type in_type = fh.getType(in);
      if (in_type == FileTypes::IDXML)
      {
        IdXMLFile().load(in, protein_ids, peptide_ids);
      }
      else if (in_type == FileTypes::MZIDENTML)
      {
        LOG_WARN << "Converting from mzid: possible loss of information depending on target format." << endl;
        MzIdentMLFile().load(in, protein_ids, peptide_ids);
      }
      //else catched by TOPPBase:registerInput being mandatory mzid or idxml
      
      //paranoia check if this comes from the same search engine! (only in the first proteinidentification of the first proteinidentifications vector vector)
      {
        ProteinIdentification::SearchParameters all_search_parameters = all_protein_ids.front().getSearchParameters();
        ProteinIdentification::SearchParameters search_parameters = protein_ids.front().getSearchParameters();
        if (!multiple_search_engines && protein_ids.front().getSearchEngine() != all_protein_ids.front().getSearchEngine())
        {
          writeLog_("Input files are not all from the same search engine, set -multiple_search_engines to allow this. Aborting!");
          return INCOMPATIBLE_INPUT_DATA;
        }
        
        if (!override_db_check && search_parameters.db != all_search_parameters.db)
        {
          writeLog_("Input files are not searched with the same protein database, set -override_db_check flag to allow this. Aborting!");
          return INCOMPATIBLE_INPUT_DATA;
        }
        
        if (protein_ids.front().getScoreType()        != all_protein_ids.front().getScoreType()        )
        {
          LOG_WARN << "Warning: differing ScoreType between input files" << endl;
        }
        if (search_parameters.digestion_enzyme        != all_search_parameters.digestion_enzyme        )
        {
          LOG_WARN << "Warning: differing DigestionEnzyme between input files" << endl;
        }
        if (search_parameters.variable_modifications  != all_search_parameters.variable_modifications  )
        {
          LOG_WARN << "Warning: differing VarMods between input files" << endl;
        }
        if (search_parameters.fixed_modifications     != all_search_parameters.fixed_modifications     )
        {
          LOG_WARN << "Warning: differing FixMods between input files" << endl;
        }
        if (search_parameters.charges                 != all_search_parameters.charges                 )
        {
          LOG_WARN << "Warning: differing SearchCharges between input files" << endl;
        }
        if (search_parameters.fragment_mass_tolerance != all_search_parameters.fragment_mass_tolerance )
        {
          LOG_WARN << "Warning: differing FragTol between input files" << endl;
        }
        if (search_parameters.precursor_tolerance     != all_search_parameters.precursor_tolerance     )
        {
          LOG_WARN << "Warning: differing PrecTol between input files" << endl;
        }
      }
      
      if (!multiple_search_engines)
      {
        all_peptide_ids.insert(all_peptide_ids.end(), peptide_ids.begin(), peptide_ids.end());
      }
      else if (concatenate)
      {
        TopPerc::concatMULTISEids(all_protein_ids, all_peptide_ids, protein_ids, peptide_ids, search_engines_used);
      }
      else
      {
        // will collapse the list (reference)
        TopPerc::mergeMULTISEids(all_protein_ids, all_peptide_ids, protein_ids, peptide_ids, search_engines_used);
      }
    }

    //-------------------------------------------------------------
    // extract search engine and prepare pin
    //-------------------------------------------------------------
    String search_engine = all_protein_ids.front().getSearchEngine();
    if (multiple_search_engines) search_engine = "multiple";
    LOG_DEBUG << "Registered search engine: " << search_engine << endl;
    
    TextFile txt;

    StringList feature_set;
    if (search_engine == "multiple")
    {
      if (getFlag_("concat"))
      {
        TopPerc::addCONCATSEFeatures(all_peptide_ids, search_engines_used, feature_set);
      }
      else
      {
        TopPerc::addMULTISEFeatures(all_peptide_ids, search_engines_used, feature_set);
      }
    }
    //TODO introduce custom feature selection from TopPerc::prepareCUSTOMpin to parameters
    else if (search_engine == "MS-GF+") TopPerc::addMSGFFeatures(all_peptide_ids, feature_set);
    else if (search_engine == "Mascot") TopPerc::addMASCOTFeatures(all_peptide_ids, feature_set);
    else if (search_engine == "XTandem") TopPerc::addXTANDEMFeatures(all_peptide_ids, feature_set);
    else if (search_engine == "Comet") TopPerc::addCOMETFeatures(all_peptide_ids, feature_set);
    else
    {
      writeLog_("No known input to create PSM features from. Aborting");
      return INCOMPATIBLE_INPUT_DATA;
    }
    
    for (vector<ProteinIdentification>::iterator it = all_protein_ids.begin(); it != all_protein_ids.end(); ++it)
    {
      ProteinIdentification::SearchParameters search_parameters = it->getSearchParameters();
      
      search_parameters.setMetaValue("feature_extractor", "TOPP_PSMFeatureExtractor");
      search_parameters.setMetaValue("extra_features", ListUtils::concatenate(feature_set, ","));
      it->setSearchParameters(search_parameters);
    }
    
    // Storing the PeptideHits with calculated q-value, pep and svm score
    if (!mzid_out.empty())
    {
      MzIdentMLFile().store(mzid_out.toQString().toStdString(), all_protein_ids, all_peptide_ids);
    }
    if (!out.empty())
    {
      IdXMLFile().store(out.toQString().toStdString(), all_protein_ids, all_peptide_ids);
    }

    writeLog_("PSMFeatureExtractor finished successfully!");
    return EXECUTION_OK;
  }

};


int main(int argc, const char** argv)
{
  PSMFeatureExtractor tool;

  return tool.main(argc, argv);
}

/// @endcond
