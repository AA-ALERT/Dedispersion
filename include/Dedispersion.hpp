// Copyright 2011 Alessio Sclocco <a.sclocco@vu.nl>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>
#include <vector>

#include <Observation.hpp>
#include <utils.hpp>

#ifndef DEDISPERSION_HPP
#define DEDISPERSION_HPP

namespace PulsarSearch {

// Sequential dedispersion
template< typename T > void dedispersion(AstroData::Observation< T > & observation, const T  * const __restrict__ input, T * const __restrict__ output, const std::vector< unsigned int > & shifts);
// OpenCL dedispersion algorithm
template< typename T > std::string * getDedispersionOpenCL(const bool localMem, const unsigned int nrSamplesPerBlock, const unsigned int nrDMsPerBlock, const unsigned int nrSamplesPerThread, const unsigned int nrDMsPerThread, const std::string & dataType, const AstroData::Observation< T > & observation, const std::vector & shifts);
// AVX dedispersion algorithm
template< typename T > std::string * getDedispersionAVX(const unsigned int nrSamplesPerThread, const unsigned int nrDMsPerThread, const AstroData::Observation< T > & observation);


// Implementations
template< typename T > void dedispersion(AstroData::Observation< T > & observation, const T * const __restrict__ input, T * const __restrict__ output, const std::vector< unsigned int > & shifts) {
	for ( unsigned int dm = 0; dm < observation.getNrDMs(); dm++ ) {
		for ( unsigned int sample = 0; sample < observation.getNrSamplesPerSecond(); sample++ ) {
			T dedispersedSample = static_cast< T >(0);

			for ( unsigned int channel = 0; channel < observation.getNrChannels(); channel++ ) {
				unsigned int shift = shifts[(dm * observation.getNrChannels()) + channel];

				dedispersedSample += input[(channel * observation.get nrSamplesPerChannel) + (sample + shift)];
			}

			output[(dm * observation.getNrSamplesPerPaddedSecond()) + sample] = dedispersedSample;
		}
	}
}

template< typename T > std::string * getDedispersionOpenCL(const bool localMem, const unsigned int nrSamplesPerBlock, const unsigned int nrDMsPerBlock, const unsigned int nrSamplesPerThread, const unsigned int nrDMsPerThread, const std::string & dataType, const AstroData::Observation< T > & observation, const std::vector & shifts) {
  std::string * code = new std::string();
  std::string sumsTemplate = string();
  std::string nrTotalSamplesPerBlock_s = isa::utils::toStringValue< unsigned int >(nrSamplesPerBlock * nrSamplesPerThread);
  std::string nrTotalDMsPerBlock_s = isa::utils::toString< unsigned int >(nrDMsPerBlock * nrDMsPerThread);
  std::string nrPaddedChannels_s = isa::utils::toString< unsigned int >(observation.getNrPaddedChannels());
  std::string nrTotalThreads_s = isa::utils::toStringValue< unsigned int >(nrSamplesPerBlock * nrDMsPerBlock);

  // Begin kernel's template
  if ( localMem ) {
    *code = "__kernel void dedispersion(__global const " + dataType + " * restrict const input, __global " + dataType + " * output, __global const unsigned int * restrict const shifts) {\n"
      "unsigned int dm = (get_group_id(1) * " + nrTotalDMsPerBlock_s + ") + (get_local_id(1) * " + isa::utils::toString< unsigned int >(nrDMsPerThread) + ");\n"
      "unsigned int sample = (get_group_id(0) * " + nrTotalSamplesPerBlock_s + ") + get_local_id(0);\n"
      "unsigned int inShMem = 0;\n"
      "unsigned int inGlMem = 0;\n"
      "unsigned int minShift = 0;\n"
      "unsigned int maxShift = 0;\n"
      "__local " + dataType + " buffer[" + isa::utils::toString< unsigned int >((nrSamplesPerBlock * nrSamplesPerThread) + (shifts[(observation.getNrDMs() - 1) * observation.getNrPaddedChannels()] - shifts[(observation.getNrDMs() - (nrDMsPerBlock * nrDMsPerThread)) * observation.getNrPaddedChannels()])) + "];\n"
      "\n"
      "<%DEFS%>"
      "\n"
      "for ( unsigned int channel = 0; channel < " + isa::utils::toString< unsigned int >(observation.getNrChannels()) + "; channel++ ) {\n"
      "minShift = shifts[((get_group_id(1) * " + nrTotalDMsPerBlock_s + ") * " + nrPaddedChannels_s + ") + channel];\n"
      "<%SHIFTS%>"
      "maxShift = shifts[(((get_group_id(1) * " + nrTotalDMsPerBlock_s + ") + " + nrTotalDMsPerBlock_s + " - 1) * " + nrPaddedChannels_s + ") + channel];\n"
      "\n"
      "inShMem = (get_local_id(1) * " + isa::utils::toString< unsigned int >(nrSamplesPerBlock) + ") + get_local_id(0);\n"
      "inGlMem = ((get_group_id(0) * " + nrTotalSamplesPerBlock_s + ") + inShMem) + minShift;\n"
      "while ( inShMem < (" + nrTotalSamplesPerBlock_s + " + (maxShift - minShift)) ) {\n"
      "buffer[inShMem] = input[(channel * " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerDispersedChannel()) + ") + inGlMem];\n"
      "inShMem += " + nrTotalThreads_s + ";\n"
      "inGlMem += " + nrTotalThreads_s + ";\n"
      "}\n"
      "barrier(CLK_LOCAL_MEM_FENCE);\n"
      "\n"
      "<%SUMS%>"
      "}\n"
      "\n"
      "<%STORES%>"
      "}";
	
    sumsTemplate = "dedispersedSample<%NUM%>DM<%DM_NUM%> += buffer[(get_local_id(0) + <%OFFSET%>) + (shiftDM<%DM_NUM%> - minShift)];\n";
  } else {
    *code = "__kernel void dedispersion(__global const " + dataType + " * restrict const input, __global " + dataType + " * output, __global const unsigned int * restrict const shifts) {\n"
      "unsigned int dm = (get_group_id(1) * " + nrTotalDMsPerBlock_s + ") + (get_local_id(1) * " + isa::utils::toString< unsigned int >(nrDMsPerThread) + ");\n"
      "unsigned int sample = (get_group_id(0) * " + nrTotalSamplesPerBlock_s + ") + get_local_id(0);\n"
      "\n"
      "<%DEFS%>"
      "\n"
      "for ( unsigned int channel = 0; channel < " + isa::utils::toString< unsigned int >(observation.getNrChannels()) + "; channel++ ) {\n"
      "<%SHIFTS%>"
      "\n"
      "<%SUMS%>"
      "}\n"
      "\n"
      "<%STORES%>"
      "}";
    
    sumsTemplate = "dedispersedSample<%NUM%>DM<%DM_NUM%> += input[(channel * " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerDispersedChannel()) + ") + (sample + <%OFFSET%> + shiftDM<%DM_NUM%>)];\n";
  }
	// End kernel's template

	string defsTemplate = dataType + " dedispersedSample<%NUM%>DM<%DM_NUM%> = 0;\n";
	string shiftsTemplate = "unsigned int shiftDM<%DM_NUM%> = shifts[((dm + <%DM_NUM%>) * " + nrPaddedChannels_s + ") + channel];\n";
	string storesTemplate = "output[((dm + <%DM_NUM%>) * " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerPaddedSecond()) + ") + (sample + <%OFFSET%>)] = dedispersedSample<%NUM%>DM<%DM_NUM%>;\n";

	string * defs = new string();
	string * shifts = new string();
	string * sums = new string();
	string * stores = new string();
	for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
		string dm_s = toString< unsigned int >(dm);
		string * temp_s = 0;

		temp_s = replace(&shiftsTemplate, "<%DM_NUM%>", dm_s);
		shifts->append(*temp_s);
		delete temp_s;
	}
	for ( unsigned int sample = 0; sample < nrSamplesPerThread; sample++ ) {
		string sample_s = toString< unsigned int >(sample);
		string offset_s = toString< unsigned int >(sample * nrSamplesPerBlock);
		string * defsDM = new string();
		string * sumsDM = new string();
		string * storesDM = new string();

		for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
			string * dm_s = toStringPointer< unsigned int >(dm);
			string * temp_s = 0;

			temp_s = replace(&defsTemplate, "<%DM_NUM%>", dm_s);
			defsDM->append(*temp_s);
			delete temp_s;
			temp_s = replace(&sumsTemplate, "<%DM_NUM%>", dm_s);
			sumsDM->append(*temp_s);
			delete temp_s;
			temp_s = replace(&storesTemplate, "<%DM_NUM%>", dm_s);
			storesDM->append(*temp_s);
			delete temp_s;
		}
		defsDM = replace(defsDM, "<%NUM%>", sample_s, true);
		defs->append(*defsDM);
		delete defsDM;
		sumsDM = replace(sumsDM, "<%NUM%>", sample_s, true);
		sumsDM = replace(sumsDM, "<%OFFSET%>", offset_s, true);
		sums->append(*sumsDM);
		delete sumsDM;
		storesDM = replace(storesDM, "<%NUM%>", sample_s, true);
		storesDM = replace(storesDM, "<%OFFSET%>", offset_s, true);
		stores->append(*storesDM);
		delete storesDM;
	}
	code = replace(code, "<%DEFS%>", *defs, true);
	code = replace(code, "<%SHIFTS%>", *shifts, true);
	code = replace(code, "<%SUMS%>", *sums, true);
	code = replace(code, "<%STORES%>", *stores, true);
	delete defs;
	delete shifts;
	delete sums;
	delete stores;

  return code;
}

template< typename T > std::string * getDedispersionAVX(const unsigned int nrSamplesPerThread, const unsigned int nrDMsPerThread, const AstroData::Observation< T > & observation) {
	string * code = new string();
	*code = "namespace PulsarSearch {\n"
		"template< typename T > void dedispersionAVX" + toString< unsigned int >(nrSamplesPerThread) + "x" + toString< unsigned int >(nrDMsPerThread) + "(const unsigned int nrSamplesPerChannel, const unsigned int nrDMs, const unsigned int nrSamplesPerSecond, const unsigned int nrChannels, const unsigned int nrSamplesPerPaddedSecond, const T  * const __restrict__ input, T * const __restrict__ output, const unsigned int * const __restrict__ shifts) {\n"
		"#pragma omp parallel for schedule(static)\n"
		"for ( unsigned int dm = 0; dm < nrDMs; dm += " + toString< unsigned int >(nrDMsPerThread) + " ) {\n"
			"#pragma omp parallel for schedule(static)\n"
			"for ( unsigned int sample = 0; sample < nrSamplesPerSecond; sample += 8 * " + toString< unsigned int >(nrSamplesPerThread) + ") {\n"
				"<%DEFS%>"
				"\n"
				"for ( unsigned int channel = 0; channel < nrChannels; channel++ ) {\n"
					"<%SHIFTS%>"
					"__m256 dispersedSample;\n"
					"\n"
					"<%SUMS%>"
				"}\n"
				"\n"
				"<%STORES%>"

			"}\n"
		"}\n"
	"}\n"
	"}";
	string defsTemplate = "__m256 dedispersedSample<%NUM%>DM<%DM_NUM%> = _mm256_setzero_ps();\n";
	string shiftsTemplate = "unsigned int shiftDM<%DM_NUM%> = shifts[((dm + <%DM_NUM%>) * nrChannels) + channel];";
	string sumsTemplate = "dispersedSample = _mm256_loadu_ps(&(input[(channel * nrSamplesPerChannel) + ((sample + <%OFFSET%>) + shiftDM<%DM_NUM%>)]));\n"
		"dedispersedSample<%NUM%>DM<%DM_NUM%> = _mm256_add_ps(dedispersedSample<%NUM%>DM<%DM_NUM%>, dispersedSample);\n";
	string storesTemplate = "_mm256_store_ps(&(output[((dm + <%DM_NUM%>) * nrSamplesPerPaddedSecond) + (sample + <%OFFSET%>)]), dedispersedSample<%NUM%>DM<%DM_NUM%>);\n";

	string * defs = new string();
	string * shifts = new string();
	string * sums = new string();
	string * stores = new string();
	for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
		string dm_s = toString< unsigned int >(dm);
		string * temp = 0;

		temp = replace(&shiftsTemplate, "<%DM_NUM%>", dm_s);
		shifts->append(*temp);
		delete temp;
	}
	for ( unsigned int sample = 0; sample < nrSamplesPerThread; sample++ ) {
		string sample_s = toString< unsigned int >(sample);
		string offset_s = toString< unsigned int >(sample * 16);
		string * defsDM = new string();
		string * sumsDM = new string();
		string * storesDM = new string();

		for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
			string dm_s = toString< unsigned int >(dm);
			string * temp = 0;

			temp = replace(&defsTemplate, "<%DM_NUM%>", dm_s);
			defsDM->append(*temp);
			delete temp;
			temp = replace(&sumsTemplate, "<%DM_NUM%>", dm_s);
			sumsDM->append(*temp);
			delete temp;
			temp = replace(&storesTemplate, "<%DM_NUM%>", dm_s);
			storesDM->append(*temp);
			delete temp;
		}
		defsDM = replace(defsDM, "<%NUM%>", sample_s, true);
		defs->append(*defsDM);
		delete defsDM;
		sumsDM = replace(sumsDM, "<%NUM%>", sample_s, true);
		sumsDM = replace(sumsDM, "<%OFFSET%>", offset_s, true);
		sums->append(*sumsDM);
		delete sumsDM;
		storesDM = replace(storesDM, "<%NUM%>", sample_s, true);
		storesDM = replace(storesDM, "<%OFFSET%>", offset_s, true);
		stores->append(*storesDM);
		delete storesDM;
	}
	code = replace(code, "<%DEFS%>", *defs, true);
	code = replace(code, "<%SHIFTS%>", *shifts, true);
	code = replace(code, "<%SUMS%>", *sums, true);
	code = replace(code, "<%STORES%>", *stores, true);
	delete defs;
	delete shifts;
	delete sums;
	delete stores;

	cout << *code << endl;
}

} // PulsarSearch

#endif // DEDISPERSION_HPP

