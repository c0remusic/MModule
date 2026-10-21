#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace md::mds
{
	struct Package;
}

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;
	struct Frame;

	// The Library in the machine browser (ticket 26 of the User Machines map, a prototype): the USR block's "choose
	// among N", its one line (no numbered cell: the Kit gives the numbers, ticket 25), opens every User Machine the Library holds over the browser's own families, laid out as those are (the
	// user's choice of 2026-10-20, "comme pour le premier menu"): the package's category at the head with its count and
	// what kind of Machines it holds, its code in each cell, 9 rows to a column at most, a longer family shared evenly
	// over two or more columns under one head; a click puts the Track shown on it (Processor::chooseUserMachine), heard
	// when listening; "Built-in machines" and Esc go back. The effects' cells say their package's name, two columns wide
	// (BUS COMP, BBD CHORUS: their codes say little; the user's wish of 2026-10-20). Families go on to the right of the
	// families' columns, above the preview, when they hold 7 rows or fewer. The cells are made again when the Library
	// changes; the one the Track plays is lit.
	class LibraryView
	{
	public:
		explicit LibraryView(Editor& _editor);
		~LibraryView();

		LibraryView(const LibraryView&) = delete;
		LibraryView& operator=(const LibraryView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

		bool isShown() const { return m_shown; }
		void show(bool _shown);
		// Esc: back to the built-in machines, true when it was showing
		bool escape();

		// What the cells hold, in their order, for the tests
		const std::vector<std::shared_ptr<const md::mds::Package>>& machines() const { return m_machines; }
		// The cell of a User Machine of the Library, for the tests; none when it shows none
		Rml::Element* cell(size_t _index) const { return _index < m_cells.size() ? m_cells[_index] : nullptr; }
		void choose(size_t _index);

		static constexpr int Rows = 9;		// 288 to 544 dp, as the built-in families' longest column
		static constexpr int Columns = 8;	// c4 to c11: the families' columns
		static constexpr int RightColumns = 5;	// c12 to c16, above the preview
		static constexpr int RightRows = 7;		// 288 to 480 dp, above the preview's head at 528

	private:
		bool build();
		bool followChosen(uint8_t _track);

		Editor& m_editor;
		Rml::Element* m_browser = nullptr;
		Rml::Element* m_library = nullptr;
		Rml::Element* m_open = nullptr;
		Rml::Element* m_count = nullptr;	// the USR head's count: how many the Library holds
		Rml::Element* m_what = nullptr;
		bool m_shown = false;
		std::vector<std::shared_ptr<const md::mds::Package>> m_machines;	// the cells', as built
		std::vector<Rml::Element*> m_cells;
		int m_chosen = -1;			// the lit cell
		double m_nextMs = 0.0;		// when the Library is looked at again
		size_t m_countShown = ~size_t{0};
	};
}
