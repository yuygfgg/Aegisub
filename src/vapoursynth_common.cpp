// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#ifdef WITH_VAPOURSYNTH
#include "vapoursynth_common.h"

#include "vapoursynth_wrap.h"
#include "options.h"
#include "utils.h"
#include <libaegisub/background_runner.h>
#include <libaegisub/format.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/scoped_ptr.h>
#include <libaegisub/util.h>

#include <boost/algorithm/string/replace.hpp>

void SetStringVar(const VSAPI *api, VSMap *map, std::string variable, std::string value) {
	if (api->mapSetData(map, variable.c_str(), value.c_str(), -1, dtUtf8, 1))
		throw VapourSynthError("Failed to set VSMap entry");
}

int OpenScriptOrVideo(const VSAPI *api, const VSSCRIPTAPI *sapi, VSScript *script, agi::fs::path const& filename, std::string default_script) {
	int result;
	if (agi::fs::HasExtension(filename, "py") || agi::fs::HasExtension(filename, "vpy")) {
		result = sapi->evaluateFile(script, filename.string().c_str());
	} else {
		agi::scoped_holder<VSMap *> map(api->createMap(), api->freeMap);
		if (map == nullptr)
			throw VapourSynthError("Failed to create VSMap for script info");

		SetStringVar(api, map, "filename", filename.string());
		auto vscache = config::path->Decode("?local/vscache");
		agi::fs::CreateDirectory(vscache);
		SetStringVar(api, map, "__aegi_vscache", vscache.string());
#ifdef WIN32
		SetStringVar(api, map, "__aegi_vsplugins", config::path->Decode("?data/vapoursynth").string());
#else
		SetStringVar(api, map, "__aegi_vsplugins", "");
#endif
		for (std::string dir : { "data", "dictionary", "local", "script", "temp", "user", })
			// Don't include ?audio and ?video in here since these only hold the paths to the previous audio/video files.
			SetStringVar(api, map, "__aegi_" + dir, config::path->Decode("?" + dir).string());

		if (sapi->setVariables(script, map))
			throw VapourSynthError("Failed to set script info variables");

		std::string vscript;
		vscript += "import sys\n";
		vscript += "sys.path.append(f'{__aegi_user}/automation/vapoursynth')\n";
		vscript += "sys.path.append(f'{__aegi_data}/automation/vapoursynth')\n";
		vscript += default_script;
		result = sapi->evaluateBuffer(script, vscript.c_str(), "aegisub");
	}
	return result;
}

void VSLogToProgressSink(int msgType, const char *msg, void *userData) {
	auto sink = reinterpret_cast<agi::ProgressSink *>(userData);

	std::string msgStr(msg);
	int commaPos = msgStr.find(',');
	if (commaPos) {
		std::string command = msgStr.substr(0, commaPos);
		std::string tail = msgStr.substr(commaPos + 1, msgStr.length());

		// We don't allow setting the title since that should stay as "Executing VapourSynth Script".
		if (command == "__aegi_set_message") {
			sink->SetMessage(tail);
		} else if (command == "__aegi_set_progress") {
			double percent;
			if (!agi::util::try_parse(tail, &percent)) {
				msgType = 2;
				msgStr = agi::format("Warning: Invalid argument to __aegi_set_progress: %s\n", tail);
			} else {
				sink->SetProgress(percent, 100);
			}
		} else if (command == "__aegi_set_indeterminate") {
			sink->SetIndeterminate();
		}
	}

	int loglevel = 0;
	std::string loglevel_str = OPT_GET("Provider/Video/VapourSynth/Log Level")->GetString();
	if (loglevel_str == "Quiet")
		loglevel = 5;
	else if (loglevel_str == "Fatal")
		loglevel = 4;
	else if (loglevel_str == "Critical")
		loglevel = 3;
	else if (loglevel_str == "Warning")
		loglevel = 2;
	else if (loglevel_str == "Information")
		loglevel = 1;
	else if (loglevel_str == "Debug")
		loglevel = 0;

	if (msgType < loglevel)
		return;

	sink->Log(msgStr);
}

void VSCleanCache() {
	CleanCache(config::path->Decode("?local/vscache/"),
		"",
		OPT_GET("Provider/VapourSynth/Cache/Size")->GetInt(),
		OPT_GET("Provider/VapourSynth/Cache/Files")->GetInt());
}

#endif // WITH_VAPOURSYNTH
