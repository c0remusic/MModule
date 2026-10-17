#include "mdDrumsDropView.h"

#include "mdDrumsEditor.h"
#include "mdDrumsStatusView.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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
			return _files.size() == 1 && takes(_files.front());
		}

		void dropFiles(const Rml::Event&, const juceRmlUi::FileDragData*, const std::vector<std::string>& _files) override
		{
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
		auto extension = std::filesystem::u8path(_file).extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](const char _c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(_c))); });
		for(const auto* known : {".wav", ".wave", ".aif", ".aiff", ".flac", ".syx", ".mds"})
			if(extension == known)
				return true;
		return false;
	}

	bool DropView::drop(const std::string& _file)
	{
		auto* status = m_editor.getStatusView();
		return status && status->importFile(std::filesystem::u8path(_file));
	}
}
