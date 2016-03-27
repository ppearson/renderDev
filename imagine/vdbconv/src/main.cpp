/*
 vdbconv
 Copyright 2014-2016 Peter Pearson.

 Licensed under the Apache License, Version 2.0 (the "License");
 You may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 ---------
*/

#include <string>
#include <stdio.h>

#include "vdb_converter.h"

int main(int argc, char** argv)
{
	openvdb::initialize();

	bool printHelp = false;

	if (argc < 3)
	{
		fprintf(stderr, "incorrects args:\n");
		printHelp = true;
	}

	unsigned int argOffset = 0;



	VDBConverter converter;

	bool sequence = false;

	unsigned int numOptionArgs = (argc - 1) - 2;

	if (!printHelp)
	{

		for (unsigned int i = 0; i < numOptionArgs; i++)
		{
			std::string argString = argv[i + 1];

			if (argString.substr(0, 1) != "-")
				continue;

			size_t startPos = argString.find_first_not_of("-");

			std::string argName = argString.substr(startPos);

			if (argName == "help")
			{
				printHelp = true;
				break;
			}
			else if (argName == "half")
			{
				converter.setStoreAsHalf(true);
			}
			else if (argName == "valMul" && numOptionArgs > i + 1)
			{
				std::string strValMultValue = argv[i + 1 + 1];
				if (!strValMultValue.empty())
				{
					float valueMultiplier = atof(strValMultValue.c_str());
					converter.setValueMultiplier(valueMultiplier);
					argOffset += 1;
				}
			}
			else if (argName == "seq")
			{
				sequence = true;
			}
			else if (argName == "sparse")
			{
				converter.setUseSparseGrid(true);
			}
			else if (argName == "dense")
			{
				converter.setUseSparseGrid(false);
			}

			argOffset += 1;
		}
	}

	if (printHelp)
	{
		fprintf(stderr, "OpenVDB to Imagine volume converter. 0.2.\n");
		fprintf(stderr, "Usage: vdbconv [options] <source_vdb> <dest_ivv>\n");
		fprintf(stderr, "    Options: -half\t\tsave as half format\n");
		fprintf(stderr, "    Options: -valMul <float>\tapply value modifier\n\n");
		return 0;
	}

	// now handle the last two args, which should be the input and output filenames...

	std::string sourceFile(argv[1 + argOffset]);
	std::string destFile(argv[2 + argOffset]);

	if (sequence && (sourceFile.find("#") == std::string::npos || destFile.find("#") == std::string::npos))
	{
		sequence = false;
	}

	if (!sequence)
	{
		converter.convertSingle(sourceFile, destFile);
	}
	else
	{
		converter.convertSequence(sourceFile, destFile);
	}

	return 0;
}

