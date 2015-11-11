// Copyright 2015 Alessio Sclocco <a.sclocco@vu.nl>
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

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <iomanip>

#include <configuration.hpp>

#include <ArgumentList.hpp>
#include <Observation.hpp>
#include <ReadData.hpp>
#include <Shifts.hpp>
#include <Dedispersion.hpp>


int main(int argc, char * argv[]) {
  // Command line arguments
  isa::utils::ArgumentList args(argc, argv);
  // Observation
  AstroData::Observation observation;
  unsigned int padding = 0;
  uint8_t inputBits = 0;
  // Files
	std::string dataFile;
	std::string headerFile;
	std::string outputFile;
  std::string channelsFile;
  std::vector< std::ofstream > output;
  // LOFAR
  bool dataLOFAR = false;
  bool limit = false;
  // SIGPROC
  bool dataSIGPROC = false;
  unsigned int bytesToSkip = 0;
  // PSRDada
  bool dataPSRDada = false;
  key_t dadaKey;
  dada_hdu_t * ringBuffer;

  try {
    padding = args.getSwitchArgument< unsigned int >("-padding");
    dataLOFAR = args.getSwitch("-lofar");
    dataSIGPROC = args.getSwitch("-sigproc");
    dataPSRDada = args.getSwitch("-dada");
		if ( !((((!(dataLOFAR && dataSIGPROC) && dataPSRDada) || (!(dataLOFAR && dataPSRDada) && dataSIGPROC)) || (!(dataSIGPROC && dataPSRDada) && dataLOFAR)) || ((!dataLOFAR && !dataSIGPROC) && !dataPSRDada)) ) {
			std::cerr << "-lofar -sigproc and -dada are mutually exclusive." << std::endl;
			throw std::exception();
		} else if ( dataLOFAR ) {
      observation.setNrBeams(1);
			headerFile = args.getSwitchArgument< std::string >("-header");
			dataFile = args.getSwitchArgument< std::string >("-data");
      limit = args.getSwitch("-limit");
      if ( limit ) {
        observation.setNrSeconds(args.getSwitchArgument< unsigned int >("-seconds"));
      }
		} else if ( dataSIGPROC ) {
      observation.setNrBeams(1);
			bytesToSkip = args.getSwitchArgument< unsigned int >("-header");
			dataFile = args.getSwitchArgument< std::string >("-data");
			observation.setNrSeconds(args.getSwitchArgument< unsigned int >("-seconds"));
      observation.setFrequencyRange(args.getSwitchArgument< unsigned int >("-channels"), args.getSwitchArgument< float >("-min_freq"), args.getSwitchArgument< float >("-channel_bandwidth"));
			observation.setNrSamplesPerSecond(args.getSwitchArgument< unsigned int >("-samples"));
    } else if ( dataPSRDada ) {
      dadaKey = args.getSwitchArgument< key_t >("-dada_key");
      observation.setNrBeams(args.getSwitchArgument< unsigned int >("-beams"));
      observation.setNrSeconds(args.getSwitchArgument< unsigned int >("-seconds"));
		}
    channelsFile = args.getSwitchArgument< std::string >("-zapped_channels");
    inputBits = args.getSwitchArgument< unsigned int >("-input_bits");
		outputFile = args.getSwitchArgument< std::string >("-output");
    observation.setDMRange(1, args.getSwitchArgument< float >("-dm"), 0.0f);
	} catch ( isa::utils::EmptyCommandLine & err ) {
    std::cerr <<  args.getName() << " -padding ... [-lofar] [-sigproc] [-dada] -zapped_channels ... -input_bits ... -output ... -dm ..." << std::endl;
    std::cerr << "\t -lofar -header ... -data ... [-limit]" << std::endl;
    std::cerr << "\t\t -limit -seconds ..." << std::endl;
    std::cerr << "\t -sigproc -header ... -data ... -seconds ... -channels ... -min_freq ... -channel_bandwidth ... -samples ..." << std::endl;
    std::cerr << "\t -dada -dada_key ... -beams ... -seconds ..." << std::endl;
    return 1;
  } catch ( std::exception & err ) {
		std::cerr << err.what() << std::endl;
		return 1;
	}

	// Load observation data
  std::vector< std::vector< std::vector< inputDataType > * > * > input(observation.getNrBeams());
  std::vector< bool > zappedChannels(observation.getNrPaddedChannels(padding / sizeof(bool)));
	if ( dataLOFAR ) {
    input[0] = new std::vector< std::vector< inputDataType > * >(observation.getNrSeconds());
    if ( limit ) {
      AstroData::readLOFAR(headerFile, dataFile, observation, padding, *(input[0]), observation.getNrSeconds());
    } else {
      AstroData::readLOFAR(headerFile, dataFile, observation, padding, *(input[0]));
    }
	} else if ( dataSIGPROC ) {
    input[0] = new std::vector< std::vector< inputDataType > * >(observation.getNrSeconds());
    AstroData::readSIGPROC(observation, padding, inputBits, bytesToSkip, dataFile, *(input[0]));
  } else if ( dataPSRDada ) {
    ringBuffer = dada_hdu_create(0);
    dada_hdu_set_key(ringBuffer, dadaKey);
    dada_hdu_connect(ringBuffer);
    dada_hdu_lock_read(ringBuffer);
	}
  AstroData::readZappedChannels(observation, channelsFile, zappedChannels);
  output = std::vector< std::ofstream >(observation.getNrBeams());

	// Host memory allocation
  std::vector< float > * shifts = PulsarSearch::getShifts(observation, padding);
  observation.setNrSamplesPerDispersedChannel(observation.getNrSamplesPerSecond() + static_cast< unsigned int >(shifts->at(0) * observation.getFirstDM()));
  observation.setNrDelaySeconds(static_cast< unsigned int >(std::ceil(static_cast< double >(observation.getNrSamplesPerDispersedChannel()) / observation.getNrSamplesPerSecond())));
  std::vector< std::vector< inputDataType > > dispersedData(observation.getNrBeams());
  std::vector< std::vector< outputDataType > > dedispersedData(observation.getNrBeams());

  for ( unsigned int beam = 0; beam < observation.getNrBeams(); beam++ ) {
    if ( inputBits >= 8 ) {
      dispersedData[beam] = std::vector< inputDataType >(observation.getNrChannels() * observation.getNrSamplesPerPaddedDispersedChannel(padding / sizeof(inputDataType)));
    } else {
      dispersedData[beam] = std::vector< inputDataType >(observation.getNrChannels() * isa::utils::pad(observation.getNrSamplesPerDispersedChannel() / (8 / inputBits), padding / sizeof(inputDataType)));
    }
    dedispersedData[beam] = std::vector< outputDataType >(observation.getNrSamplesPerPaddedSecond(padding / sizeof(outputDataType)));
  }

  // Open output files
  for ( unsigned int beam = 0; beam < observation.getNrBeams(); beam++ ) {
    output[beam].open(outputFile + "_B" + isa::utils::toString(beam) + ".tim");
    output[beam] << std::fixed << std::setprecision(6);
  }

  // Dedispersion loop
  for ( unsigned int second = 0; second < observation.getNrSeconds() - observation.getNrDelaySeconds(); second++ ) {
    for ( unsigned int beam = 0; beam < observation.getNrBeams(); beam++ ) {
      for ( unsigned int channel = 0; channel < observation.getNrChannels(); channel++ ) {
        for ( unsigned int chunk = 0; chunk < observation.getNrDelaySeconds() - 1; chunk++ ) {
          if ( inputBits >= 8 ) {
            std::memcpy(reinterpret_cast< void * >(&(dispersedData[beam].data()[(channel * observation.getNrSamplesPerPaddedDispersedChannel(padding / sizeof(inputDataType))) + (chunk * observation.getNrSamplesPerSecond())])), reinterpret_cast< void * >(&((input[beam]->at(second + chunk))->at(channel * observation.getNrSamplesPerPaddedSecond(padding / sizeof(inputDataType))))), observation.getNrSamplesPerSecond() * sizeof(inputDataType));
          } else {
            std::memcpy(reinterpret_cast< void * >(&(dispersedData[beam].data()[(channel * isa::utils::pad(observation.getNrSamplesPerDispersedChannel() / (8 / inputBits), padding / sizeof(inputDataType))) + (chunk * (observation.getNrSamplesPerSecond() / (8 / inputBits)))])), reinterpret_cast< void * >(&((input[beam]->at(second + chunk))->at(channel * isa::utils::pad(observation.getNrSamplesPerSecond() / (8 / inputBits), padding / sizeof(inputDataType))))), (observation.getNrSamplesPerSecond() / (8 / inputBits)) * sizeof(inputDataType));
          }
        }
        if ( observation.getNrSamplesPerDispersedChannel() % observation.getNrSamplesPerSecond() == 0 ) {
          if ( inputBits >= 8 ) {
            std::memcpy(reinterpret_cast< void * >(&(dispersedData[beam].data()[(channel * observation.getNrSamplesPerPaddedDispersedChannel(padding / sizeof(inputDataType))) + ((observation.getNrDelaySeconds() - 1) * observation.getNrSamplesPerSecond())])), reinterpret_cast< void * >(&((input[beam]->at(second + (observation.getNrDelaySeconds() - 1)))->at(channel * observation.getNrSamplesPerPaddedSecond(padding / sizeof(inputDataType))))), observation.getNrSamplesPerDispersedChannel() * sizeof(inputDataType));
          } else {
            std::memcpy(reinterpret_cast< void * >(&(dispersedData[beam].data()[(channel * isa::utils::pad(observation.getNrSamplesPerDispersedChannel() / (8 / inputBits), padding / sizeof(inputDataType))) + ((observation.getNrDelaySeconds() - 1) * (observation.getNrSamplesPerSecond() / (8 / inputBits)))])), reinterpret_cast< void * >(&((input[beam]->at(second + (observation.getNrDelaySeconds() - 1)))->at(channel * isa::utils::pad(observation.getNrSamplesPerSecond() / (8 / inputBits), padding / sizeof(inputDataType))))), (observation.getNrSamplesPerDispersedChannel() / (8 / inputBits)) * sizeof(inputDataType));
          }
        } else {
          if ( inputBits >= 8 ) {
            std::memcpy(reinterpret_cast< void * >(&(dispersedData[beam].data()[(channel * observation.getNrSamplesPerPaddedDispersedChannel(padding / sizeof(inputDataType))) + ((observation.getNrDelaySeconds() - 1) * observation.getNrSamplesPerSecond())])), reinterpret_cast< void * >(&((input[beam]->at(second + (observation.getNrDelaySeconds() - 1)))->at(channel * observation.getNrSamplesPerPaddedSecond(padding / sizeof(inputDataType))))), (observation.getNrSamplesPerDispersedChannel() % observation.getNrSamplesPerSecond()) * sizeof(inputDataType));
          } else {
            std::memcpy(reinterpret_cast< void * >(&(dispersedData[beam].data()[(channel * isa::utils::pad(observation.getNrSamplesPerDispersedChannel() / (8 / inputBits), padding / sizeof(inputDataType))) + ((observation.getNrDelaySeconds() - 1) * (observation.getNrSamplesPerSecond() / (8 / inputBits)))])), reinterpret_cast< void * >(&((input[beam]->at(second + (observation.getNrDelaySeconds() - 1)))->at(channel * isa::utils::pad(observation.getNrSamplesPerSecond() / (8 / inputBits), padding / sizeof(inputDataType))))), ((observation.getNrSamplesPerDispersedChannel() % observation.getNrSamplesPerSecond()) / (8 / inputBits)) * sizeof(inputDataType));
          }
        }
      }
      PulsarSearch::dedispersion< inputDataType, intermediateDataType, outputDataType >(observation, zappedChannels, dispersedData[beam], dedispersedData[beam], *shifts, padding, inputBits);
      for ( unsigned int sample = 0; sample < observation.getNrSamplesPerSecond(); sample++ ) {
        output[beam] << static_cast< uint64_t >((second * observation.getNrSamplesPerSecond()) + sample) * observation.getSamplingRate() << " " << dedispersedData[beam][sample] << std::endl;
      }
    }
  }
  if ( dataPSRDada ) {
    dada_hdu_unlock_read(ringBuffer);
    dada_hdu_disconnect(ringBuffer);
  }

  // Close output files
  for ( unsigned int beam = 0; beam < observation.getNrBeams(); beam++ ) {
    output[beam].close();
  }

  return 0;
}

