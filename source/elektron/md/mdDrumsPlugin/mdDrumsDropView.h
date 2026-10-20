#pragma once

#include "juceRmlUi/rmlDragTarget.h"

#include <memory>
#include <string>
#include <vector>

namespace mdDrums
{
	class Editor;

	// Where a file dropped on the editor goes (ticket 06 of the Samples map, the drop of ticket 16 of the User Machines
	// map): the whole window takes one audio file (WAV, AIFF, FLAC) or one User Machine (.syx, .mds) for the Track shown
	// (StatusView::importFile), the head band's machine name and the Hit screen's title lit while a file that would go
	// hovers over it (body:dragovercenter in the skin). The machine's name and the Hit screen alone, as first
	// prototyped, did not take the drops tried in Live.
	class DropView
	{
	public:
		explicit DropView(Editor& _editor);
		~DropView();

		DropView(const DropView&) = delete;
		DropView& operator=(const DropView&) = delete;

		// Whether a file of this name would go: one of the extensions importFile takes
		static bool takes(const std::string& _file);
		// A drop, as the targets get it, for the tests: true when it went
		bool drop(const std::string& _file);
		// With MD_DROP_LOG naming a file, what a drag hands over appended to it (ticket 07 of the Samples map)
		void journal(const char* _what, const std::vector<std::string>& _files);

	private:
		class Target;

		Editor& m_editor;
		std::unique_ptr<Target> m_target;
		std::string m_journalLast;
	};
}
