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

#define __CL_ENABLE_EXCEPTIONS
#include <CL/cl.hpp>
#include <string>

#include <utils.hpp>
#include <Observation.hpp>


#ifndef DEDISPERSION_HPP
#define DEDISPERSION_HPP

namespace PulsarSearch {

// OpenCL dedispersion algorithm
std::string * getDedispersionOpenCL(bool localMem, unsigned int nrSamplesPerBlock, unsigned int nrDMsPerBlock, unsigned int nrSamplesPerThread, unsigned int nrDMsPerThread, unsigned int inputChannelSize, std::string &dataType, AstroData::Observation &observation, std::vector & shifts);

// Implementation
std::string * getDedispersionOpenCL(bool localMem, unsigned int nrSamplesPerBlock, unsigned int nrDMsPerBlock, unsigned int nrSamplesPerThread, unsigned int nrDMsPerThread, unsigned int inputChannelSize, std::string &dataType, AstroData::Observation &observation, std::vector & shifts) {
  std::string * code = new std::string();
  std::string nrTotalSamplesPerBlock = isa::utils::toStringValue< unsigned int >(nrSamplesPerBlock * nrSamplesPerThread);
  std::string nrTotalDMsPerBlock = isa::utils::toString< unsigned int >(nrDMsPerBlock * nrDMsPerThread);
  std::string nrPaddedChannels = isa::utils::toString< unsigned int >(observation.getNrPaddedChannels());
  std::string nrTotalThreads = isa::utils::toStringValue< unsigned int >(nrSamplesPerBlock * nrDMsPerBlock);

	*code = "__kernel void dedispersion(__global const " + dataType + " * restrict const input, __global " + dataType + " * output, __global const unsigned int * restrict const shifts) {\n"
		"unsigned int dm = (get_group_id(1) * " + nrTotalDMsPerBlock + ") + (get_local_id(1) * " + isa::utils::toString< unsigned int >(nrDMsPerThread) + ");\n"
		"unsigned int sample = (get_group_id(0) * " + nrTotalSamplesPerBlock + ") + get_local_id(0);\n"
		"unsigned int inShMem = 0;\n"
		"unsigned int inGlMem = 0;\n"
		"unsigned int minShift = 0;\n"
		"unsigned int maxShift = 0;\n"
		"__local " + dataType + " buffer[" + isa::utils::toString< unsigned int >((nrSamplesPerBlock * nrSamplesPerThread) + (shifts[(observation.getNrDMs() - 1) * observation.getNrPaddedChannels()] - shifts[(observation.getNrDMs() - (nrDMsPerBlock * nrDMsPerThread)) * observation.getNrPaddedChannels()])) + "];\n"
		"\n"
		"<%DEFS%>"
		"\n"
		"for ( unsigned int channel = 0; channel < " + isa::utils::toString< unsigned int >(observation.getNrChannels()) + "; channel++ ) {\n"
		"minShift = shifts[((get_group_id(1) * " + nrTotalDMsPerBlock + ") * " + nrPaddedChannels + ") + channel];\n"
		"<%SHIFTS%>"
		"maxShift = shifts[(((get_group_id(1) * " + nrTotalDMsPerBlock + ") + " + nrTotalDMsPerBlock + " - 1) * " + nrPaddedChannels + ") + channel];\n"
		"\n"
		"inShMem = (get_local_id(1) * " + isa::utils::toString< unsigned int >(nrSamplesPerBlock) + ") + get_local_id(0);\n"
		"inGlMem = ((get_group_id(0) * " + nrTotalSamplesPerBlock + ") + inShMem) + minShift;\n"
		"while ( inShMem < (" + nrTotalSamplesPerBlock + " + (maxShift - minShift)) ) {\n"
		"buffer[inShMem] = input[(channel * " + isa::utils::toString< unsigned int >(inputChannelSize) + ") + inGlMem];\n"
		"inShMem += " + nrTotalThreads + ";\n"
		"inGlMem += " + nrTotalThreads + ";\n"
		"}\n"
		"barrier(CLK_LOCAL_MEM_FENCE);\n"
		"\n"
		"<%SUMS%>"
		"}\n"
		"\n"
		"<%STORES%>"
		"}";

	string defsTemplate = dataType + " dedispersedSample<%NUM%>DM<%DM_NUM%> = 0;\n";
	string shiftsTemplate = "unsigned int shiftDM<%DM_NUM%> = shifts[((dm + <%DM_NUM%>) * " + nrPaddedChannels + ") + channel];\n";
	string sumsTemplate = "dedispersedSample<%NUM%>DM<%DM_NUM%> += buffer[(get_local_id(0) + <%OFFSET%>) + (shiftDM<%DM_NUM%> - minShift)];\n";
	string storesTemplate = "output[((dm + <%DM_NUM%>) * " + isa::utils::toString< unsigned int >(observation.getNrSamplesPerPaddedSecond()) + ") + (sample + <%OFFSET%>)] = dedispersedSample<%NUM%>DM<%DM_NUM%>;\n";
	// End kernel's template

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
		string offset_s = toString< unsigned int >(sample * nrSamplesPerBlock);
		string * defsDM = new string();
		string * sumsDM = new string();
		string * storesDM = new string();

		for ( unsigned int dm = 0; dm < nrDMsPerThread; dm++ ) {
			string * dm_s = toStringPointer< unsigned int >(dm);
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
}

} // PulsarSearch

#endif // DEDISPERSION_HPP
