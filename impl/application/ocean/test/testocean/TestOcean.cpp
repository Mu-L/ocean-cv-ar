/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "application/ocean/test/testocean/TestOcean.h"

#include "ocean/base/Build.h"
#include "ocean/base/CommandArguments.h"
#include "ocean/base/DateTime.h"
#include "ocean/base/PluginManager.h"
#include "ocean/base/Processor.h"
#include "ocean/base/String.h"
#include "ocean/base/Utilities.h"
#include "ocean/base/Worker.h"

#include "ocean/math/Random.h"

#include "ocean/platform/System.h"

#include "ocean/system/Memory.h"
#include "ocean/system/OperatingSystem.h"
#include "ocean/system/Process.h"

#include "ocean/test/testbase/TestBase.h"

#include "ocean/test/testcv/TestCV.h"

#include "ocean/test/testcv/testadvanced/TestCVAdvanced.h"

#include "ocean/test/testcv/testdetector/TestCVDetector.h"

#include "ocean/test/testcv/testsegmentation/TestCVSegmentation.h"

//#include "ocean/test/testcv/testsynthesis/TestCVSynthesis.h"

#include "ocean/test/testgeometry/TestGeometry.h"

#include "ocean/test/testmath/TestMath.h"

//#include "ocean/test/testrendering/TestRendering.h"

#ifdef OCEAN_RUNTIME_STATIC
	#if defined(__APPLE__)
		#include "ocean/media/imageio/ImageIO.h"
	#elif defined(_WINDOWS)
		#include "ocean/media/wic/WIC.h"
	#endif
#endif

using namespace Ocean;

#if defined(_WINDOWS)
	// main function on Windows platforms
	int wmain(int argc, wchar_t* argv[])
#elif defined(__APPLE__) || defined(__linux__)
	// main function on OSX and Linux platforms
	int main(int argc, char* argv[])
#else
	#error Missing implementation.
#endif
{
#ifdef OCEAN_COMPILER_MSC
	// prevent the debugger to abort the application after an assert has been caught
	_set_error_mode(_OUT_TO_MSGBOX);
#endif

	const std::string frameworkPath(Platform::System::environmentVariable("OCEAN_DEVELOPMENT_PATH"));

#ifdef OCEAN_DEBUG
	constexpr double defaultTestDuration = 0.1;
#else
	constexpr double defaultTestDuration = 2.0;
#endif // OCEAN_DEBUG

	CommandArguments commandArguments;
	commandArguments.registerParameter("image", "i", "The test image filename, e.g., \"image.png\"", Value(frameworkPath + std::string("/res/application/ocean/test/cv/testcv/testdetector/tropical-island-with-toucans_800x800.jpg")));
	commandArguments.registerParameter("output", "o", "The optional output file for the test log, e.g., log.txt");
	commandArguments.registerParameter("libraries", "l", "The optional subset of libraries to test, e.g., \"cv, geometry\"");
	commandArguments.registerParameter("duration", "d", "The test duration for each test in seconds, e.g., 1.0", Value(defaultTestDuration));
	commandArguments.registerParameter("waitForKey", "wfk", "Wait for a key input before the application exits");
	commandArguments.registerParameter("help", "h", "Show this help output");

	commandArguments.parse(argv, size_t(argc));

	if (commandArguments.hasValue("help", nullptr, false))
	{
		std::cout << "Ocean Framework test:" << std::endl << std::endl;
		std::cout << commandArguments.makeSummary() << std::endl;
		return 0;
	}

	std::string mediaFilename = frameworkPath + std::string("/res/application/ocean/test/cv/testcv/testdetector/tropical-island-with-toucans_800x800.jpg");
	double testDuration = defaultTestDuration;
	std::string outputFilename;
	std::string libraryList;

	Value imageValue;
	if (commandArguments.hasValue("image", &imageValue, true) && imageValue.isString())
	{
		mediaFilename = imageValue.stringValue();
	}

	Value durationValue;
	if (commandArguments.hasValue("duration", &durationValue, true) && durationValue.isFloat64(true))
	{
		testDuration = durationValue.float64Value(true);
	}

	Value outputValue;
	if (commandArguments.hasValue("output", &outputValue) && outputValue.isString())
	{
		outputFilename = outputValue.stringValue();
	}

	Value librariesValue;
	if (commandArguments.hasValue("libraries", &librariesValue) && librariesValue.isString())
	{
		libraryList = librariesValue.stringValue();
	}

	if (outputFilename.empty() || outputFilename == "STANDARD")
	{
		Messenger::get().setOutputType(Messenger::OUTPUT_STANDARD);
	}
	else
	{
		Messenger::get().setOutputType(Messenger::OUTPUT_FILE);
		Messenger::get().setFileOutput(outputFilename);
	}

	const Timestamp startTimestamp(true);

	Log::info() << "Ocean Framework test:";
	Log::info() << " ";
	Log::info() << "Platform: " << Build::buildString();
	Log::info() << " ";
	Log::info() << "Start: " << DateTime::stringDate() << ", " << DateTime::stringTime() << " UTC";
	Log::info() << " ";

	Log::info() << "Library list: " << (libraryList.empty() ? "All libraries" : libraryList);
	Log::info() << "Duration for each test: " << String::toAString(testDuration, 1u) << "s";
	Log::info() << " ";

	RandomI::initialize();
	System::Process::setPriority(System::Process::PRIORITY_ABOVE_NORMAL);

	Log::info() << "Random generator initialized";
	Log::info() << "Process priority set to above normal";
	Log::info() << " ";

	Worker worker;

	Log::info() << "Operating System: " << System::OperatingSystem::name();
	Log::info() << "Processor: " << Processor::brand();
	Log::info() << "Used worker threads: " << worker.threads();
	Log::info() << "Test with: " << String::toAString(sizeof(Scalar)) << "byte floats";
	Log::info() << " ";

	const unsigned long long startVirtualMemory = System::Memory::processVirtualMemory();

	Log::info() << "Currently used memory: " << String::insertCharacter(String::toAString(startVirtualMemory >> 10), ',', 3, false) << "KB";
	Log::info() << " ";

	unsigned int startedTests = 0u;
	unsigned int succeededTests = 0u;

	Strings tests(Utilities::separateValues(String::toLower(libraryList), ',', true, true));
	const std::set<std::string> testSet(tests.begin(), tests.end());

	try
	{
		if (testSet.empty() || testSet.find("base") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestBase::testBase(testDuration, worker))
			{
				succeededTests++;
			}
		}

		if (testSet.empty() || testSet.find("math") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestMath::testMath(testDuration, worker))
			{
				succeededTests++;
			}
		}

		if (testSet.empty() || testSet.find("cv") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestCV::testCV(testDuration, worker))
			{
				succeededTests++;
			}
		}

		if (testSet.empty() || testSet.find("geometry") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestGeometry::testGeometry(testDuration, worker))
			{
				succeededTests++;
			}
		}

		if (testSet.empty() || testSet.find("cvadvanced") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestCV::TestAdvanced::testCVAdvanced(testDuration, worker))
			{
				succeededTests++;
			}
		}

		if (testSet.empty() || testSet.find("cvdetector") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestCV::TestDetector::testCVDetector(testDuration, worker, mediaFilename))
			{
				succeededTests++;
			}
		}

		if (testSet.empty() || testSet.find("cvsegmentation") != testSet.end())
		{
			startedTests++;

			Log::info() << "\n\n\n\n\n\n";
			if (Test::TestCV::TestSegmentation::testCVSegmentation(testDuration, worker))
			{
				succeededTests++;
			}
		}
	}
	catch (...)
	{
		ocean_assert(false && "Unhandled exception!");
		Log::info() << "Unhandled exception!";
	}

	ocean_assert(succeededTests <= startedTests);

	const unsigned long long stopVirtualMemory = System::Memory::processVirtualMemory();

	Log::info() << " ";
	Log::info() << "Currently used memory: " << String::insertCharacter(String::toAString(stopVirtualMemory >> 10), ',', 3, false) << "KB (+ " << String::insertCharacter(String::toAString((stopVirtualMemory - startVirtualMemory) >> 10), ',', 3, false) << "KB)";
	Log::info() << " ";

	const Timestamp endTimestamp(true);

	Log::info() << "Time elapsed: " << DateTime::seconds2string(double(endTimestamp - startTimestamp), true);
	Log::info() << "End: " << DateTime::stringDate() << ", " << DateTime::stringTime() << " UTC";
	Log::info() << " ";

	if (succeededTests == startedTests)
	{
		Log::info() << (testSet.empty() ? "Entire" : "Partial") << " Ocean Framework test succeeded.";
	}
	else
	{
		Log::info() << (testSet.empty() ? "Entire" : "Partial") << " Ocean Framework test FAILED!";
	}

	Log::info() << " ";

	if (commandArguments.hasValue("waitForKey"))
	{
		Log::info() << "Press a key to exit.";
		getchar();
	}

	if (startedTests == succeededTests)
	{
		return 0;
	}

	return 1;
}
