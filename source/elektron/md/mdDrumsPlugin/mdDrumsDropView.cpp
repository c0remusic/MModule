#include "mdDrumsDropView.h"

#include "mdDrumsEditor.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsStatusView.h"

#include "mdLib/mdenv.h"

#include <filesystem>
#include <fstream>

namespace mdDrums
{
	class DropView::Target final : public juceRmlUi::DragTarget
	{
	public:
		Target(DropView& _view, Rml::Element* _element) : DragTarget(_element), m_view(_view)
		{
			setAllowLocations(false, false);
		}

		bool canDropFiles(const Rml::Event&, const std::vector<std::string>& _files) override
		{
			m_view.journal("over", _files);
			return _files.size() == 1 && takes(_files.front());
		}

		void dropFiles(const Rml::Event&, const juceRmlUi::FileDragData*, const std::vector<std::string>& _files) override
		{
			m_view.journal("drop", _files);
			if(_files.size() == 1)
				m_view.drop(_files.front());
		}

	private:
		DropView& m_view;
	};

	DropView::DropView(Editor& _editor) : m_editor(_editor)
	{
		for(const auto* id : {"machine", "screen_hit"})
			if(auto* element = m_editor.element(id))
				m_targets.push_back(std::make_unique<Target>(*this, element));
	}

	DropView::~DropView() = default;

	bool DropView::takes(const std::string& _file)
	{
		return Processor::importKind(std::filesystem::u8path(_file)) != Processor::ImportKind::None;
	}

	void DropView::journal(const char* _what, const std::vector<std::string>& _files)
	{
		// Ticket 07 of the Samples map: what Live hands over when a clip, a Simpler's sample or a Drum Rack pad is dragged
		// here, written to the file MD_DROP_LOG names, a line per new list (hovering repeats it)
		const auto* path = md::envOverride("MD_DROP_LOG");
		if(!path)
			return;
		std::string line = _what;
		line += " " + std::to_string(_files.size());
		for(const auto& file : _files)
			line += " | " + file;
		if(line == m_journalLast)
			return;
		m_journalLast = line;
		std::ofstream out(std::filesystem::u8path(path), std::ios::app);
		out << line << "\n";
	}

	bool DropView::drop(const std::string& _file)
	{
		auto* status = m_editor.getStatusView();
		return status && status->importFile(std::filesystem::u8path(_file));
	}
}
