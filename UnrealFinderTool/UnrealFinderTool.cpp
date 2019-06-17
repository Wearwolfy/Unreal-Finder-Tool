#include "pch.h"
#include "GnamesFinder.h"
#include "GObjectsFinder.h"
#include "ClassFinder.h"
#include "InstanceLogger.h"
#include "SdkGenerator.h"

#include "UiWindow.h"
#include "ImGUI/imgui_internal.h"
#include "ImControl.h"
#include "IconsFontAwesome.h"
#include "MemoryEditor.h"

#include "Memory.h"
#include "Debug.h"
#include "Scanner.h"

#include <sstream>
#include <shellapi.h>

MemoryEditor mem_edit;
bool memory_init = false;
float LeftWidth, RightWidth;

void SetupMemoryStuff(const HANDLE pHandle)
{
	// Setup Memory Stuff
	if (!memory_init)
	{
		memory_init = true;
		Utils::MemoryObj = new Memory(pHandle, use_kernal);
		if (!use_kernal) Utils::MemoryObj->GetDebugPrivileges();

		// Grab engine version information
		Utils::UnrealEngineVersion(game_ue_version);

		// Setup memory editor
		mem_edit.OptMidColsCount = Utils::PointerSize();
		mem_edit.PreviewDataType = Utils::MemoryObj->Is64Bit ? MemoryEditor::DataType_S64 : MemoryEditor::DataType_S32;
	}
}

bool IsReadyToGo()
{
	HANDLE pHandle;
	if (Memory::IsValidProcess(process_id, &pHandle))
	{
		SetupMemoryStuff(pHandle);
		return true;
	}
	return false;
}

#pragma region Address Viewer
PBYTE PCurrentAddressData = nullptr;
int BufSize = 0x200;
uintptr_t CurrentViewerAddress = uintptr_t(0x1000000000);

MemoryEditor::u8 AddressViewerReadFn(const MemoryEditor::u8* data, const size_t off)
{
	if (!PCurrentAddressData)
		return 0;

	return PCurrentAddressData[off];
}

void GoToAddress(const uintptr_t address)
{
	if (Utils::MemoryObj)
	{
		// Only alloc once
		if (!PCurrentAddressData)
			PCurrentAddressData = new BYTE[BufSize];

		Utils::MemoryObj->ReadBytes(address, PCurrentAddressData, BufSize);
		CurrentViewerAddress = address;
	}
}
#pragma endregion

void BeforeWork()
{
	// Override UE4 Engine Structs
	Utils::OverrideLoadedEngineCore(unreal_versions[ue_selected_version]);

	DisabledAll();
}

void AfterWork()
{
	EnabledAll();
}

#pragma region Work Functions
void StartGObjFinder(const bool easyMethod)
{
	g_obj_listbox_items.clear();
	std::thread t([=]()
	{
		BeforeWork();
		g_objects_find_disabled = true;
		g_objects_disabled = false;
		g_names_disabled = false;
		g_obj_listbox_items.emplace_back("Searching...");
		g_obj_listbox_item_current = 0;

		GObjectsFinder taf(easyMethod);
		std::vector<uintptr_t> ret = taf.Find();
		g_obj_listbox_items.clear();

		for (auto v : ret)
		{
			std::stringstream ss;
			ss << std::hex << v;

			std::string tmpUpper = ss.str();
			std::transform(tmpUpper.begin(), tmpUpper.end(), tmpUpper.begin(), toupper);

			g_obj_listbox_items.push_back(tmpUpper);
		}

		if (ret.size() == 1)
			strcpy_s(g_objects_buf, sizeof g_objects_buf, g_obj_listbox_items[0].data());

		g_objects_find_disabled = false;
		AfterWork();
	});
	t.detach();
}

void StartGNamesFinder()
{
	g_names_listbox_items.clear();
	g_names_listbox_items.emplace_back("Searching...");

	BeforeWork();
	g_names_find_disabled = true;
	g_objects_disabled = false;
	g_names_disabled = false;

	std::thread t([&]()
	{
		GNamesFinder gf;
		uintptr_t gname_address = gf.Find()[0]; // always return one address

		g_names_listbox_items.clear();

		if (gname_address != NULL)
		{
			// Convert to hex string
			std::stringstream ss; ss << std::hex << gname_address;

			// Make hex char is Upper
			std::string tmpUpper = ss.str();
			std::transform(tmpUpper.begin(), tmpUpper.end(), tmpUpper.begin(), toupper);

			// Set value for UI
			g_names_listbox_items.push_back(tmpUpper);
			strcpy_s(g_names_buf, sizeof g_names_buf, g_names_listbox_items[0].data());
		}

		g_names_find_disabled = false;
		AfterWork();
	});
	t.detach();
}

void StartClassFinder()
{
	bool contin = false;
	// Check Address
	if (!Utils::IsValidGNamesAddress(g_names_address))
		popup_not_valid_gnames = true;
	else if (!Utils::IsValidGObjectsAddress(g_objects_address))
		popup_not_valid_gobjects = true;
	else
		contin = true;

	if (!contin || std::string(class_find_buf).empty())
		return;

	class_listbox_items.clear();
	std::thread t([&]()
	{
		BeforeWork();
		class_find_disabled = true;

		ClassFinder cf;
		class_listbox_items = cf.Find(g_objects_address, g_names_address, class_find_buf);

		class_find_disabled = false;
		AfterWork();
	});
	t.detach();
}

void StartInstanceLogger()
{
	BeforeWork();
	il_objects_count = 0;
	il_names_count = 0;
	il_state = "Running . . .";

	std::thread t([&]()
	{
		InstanceLogger il(g_objects_address, g_names_address);
		auto retState = il.Start();

		switch (retState.State)
		{
		case LoggerState::Good:
			il_state = "Finished.!!";
			break;
		case LoggerState::BadGObject:
		case LoggerState::BadGObjectAddress:
			il_state = "Wrong (GObjects) Address.!!";
			break;
		case LoggerState::BadGName:
		case LoggerState::BadGNameAddress:
			il_state = "Wrong (GNames) Address.!!";
			break;
		}

		il_objects_count = retState.GObjectsCount;
		il_names_count = retState.GNamesCount;
		AfterWork();
	});
	t.detach();
}

void StartSdkGenerator()
{
	BeforeWork();
	g_objects_find_disabled = true;
	g_names_find_disabled = true;

	sg_objects_count = 0;
	sg_names_count = 0;
	sg_packages_count = 0;
	sg_packages_done_count = 0;
	sg_state = "Running . . .";

	std::thread t([&]()
	{
		SdkGenerator sg(g_objects_address, g_names_address);
		GeneratorState ret = sg.Start(&sg_objects_count,
		                              &sg_names_count,
		                              &sg_packages_count,
		                              &sg_packages_done_count,
		                              sg_game_name_buf,
		                              std::to_string(sg_game_version[0]) + "." + std::to_string(sg_game_version[1]) +
		                              "." + std::to_string(sg_game_version[2]),
		                              static_cast<SdkType>(sg_type_item_current),
		                              sg_state, sg_packages_items);

		if (ret == GeneratorState::Good)
		{
			sg_finished = true;
			sg_state = "Finished.!!";
			Utils::UiMainWindow->FlashWindow();
		}
		else if (ret == GeneratorState::BadGObject)
			sg_state = "Wrong (GObjects) Address.!!";
		else if (ret == GeneratorState::BadGName)
			sg_state = "Wrong (GNames) Address.!!";

		AfterWork();
	});
	t.detach();
}
#pragma endregion

#pragma region User Interface
void TitleBar(UiWindow* thiz)
{
	// ui::ShowDemoWindow();

	// Settings Button
	{
		if (ui::Button(ICON_FA_COG))
			ui::OpenPopup("SettingsMenu");

		if (ui::BeginPopup("SettingsMenu"))
		{
			if (ImGui::BeginMenu("Process##menu"))
			{
				if (ui::MenuItem("Pause Process", "", &process_controller_toggles[0]))
				{
					if (!IsReadyToGo())
					{
						process_controller_toggles[0] = false;
						popup_not_valid_process = true;
					}
					else
					{
						if (process_controller_toggles[0])
							Utils::MemoryObj->SuspendProcess();
						else
							Utils::MemoryObj->ResumeProcess();
					}
				}
				ui::EndMenu();
			}

			if (ImGui::BeginMenu("SDK##menu"))
			{
				if (ui::MenuItem("SDK Folder"))
				{
					ShellExecute(nullptr,
						"open",
						(Utils::GetWorkingDirectory() + "\\Results").c_str(), 
						nullptr,
						nullptr,
						SW_SHOWDEFAULT);
				}

				ui::EndMenu();
			}

			ui::EndPopup();
		}
	}

	// Title
	{
		ui::SameLine();
		ui::SetCursorPosX(abs(ui::CalcTextSize("Unreal Finder Tool By CorrM").x - ui::GetWindowWidth()) / 2);
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Unreal Finder Tool By CorrM");
	}
}

void InformationSection(UiWindow* thiz)
{
	// Process ID
	{
		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Process ID : ");
		ui::SameLine();
		ui::SetNextItemWidth(LeftWidth / 2.f);
		ENABLE_DISABLE_WIDGET(ui::InputInt("##ProcessID", &process_id), process_id_disabled);
		ui::SameLine();

		ENABLE_DISABLE_WIDGET_IF(ui::Button(ICON_FA_SEARCH "##ProcessAutoDetector"), process_detector_disabled,
		{
			process_id = Utils::DetectUnrealGameId();
		});
	}

	// Use Kernel
	{
		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Use Kernel : ");
		ui::SameLine();
		ENABLE_DISABLE_WIDGET(ui::Checkbox("##UseKernal", &use_kernal), use_kernal_disabled);
	}

	// GObjects Address
	{
		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "GObjects   : ");
		ui::SameLine();
		ui::SetNextItemWidth(LeftWidth / 2.4f);
		ENABLE_DISABLE_WIDGET(ui::InputText("##GObjects", g_objects_buf, IM_ARRAYSIZE(g_objects_buf), ImGuiInputTextFlags_CharsHexadecimal), g_objects_disabled);
		ui::SameLine();
		HelpMarker("What you can put here .?\n- First UObject address.\n- First GObjects chunk address.\n\n* Not GObjects pointer.\n* It's the address you get from this tool.");
		g_objects_address = Utils::CharArrayToUintptr(g_objects_buf);
		ui::SameLine();
		if (ui::Button(ICON_FA_EYE"##view address gobjects") && g_objects_address != NULL)
			GoToAddress(g_objects_address);
	}

	// GNames Address
	{
		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "GNames     : ");
		ui::SameLine();


		bool style_pushed = false;
		if (!g_names_disabled)
		{
			style_pushed = true;
			if (Utils::IsValidGNamesAddress(g_names_address))
				ui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
			else
				ui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
		}

		ui::SetNextItemWidth(LeftWidth / 2.4f);
		ENABLE_DISABLE_WIDGET(ui::InputText("##GNames", g_names_buf, IM_ARRAYSIZE(g_names_buf), ImGuiInputTextFlags_CharsHexadecimal), g_names_disabled);
		g_names_address = Utils::CharArrayToUintptr(g_names_buf);

		if (style_pushed)
			ui::PopStyleColor();

		ui::SameLine();
		HelpMarker("What you can put here .?\n- GNames chunk array address.\n\n* Not GNames pointer.\n* It's the address you get from this tool.");
		ui::SameLine();
		if (ui::Button(ICON_FA_EYE"##view address gnames") && g_names_address != NULL)
			GoToAddress(g_names_address);
	}

	// Unreal version
	{
		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "UE Version : ");
		if (ui::IsItemHovered())
		{
			ui::BeginTooltip();
			ui::PushTextWrapPos(ui::GetFontSize() * 35.0f);
			ui::Text("%s", game_ue_version.c_str());
			ui::PopTextWrapPos();
			ui::EndTooltip();
		}
		ui::SameLine();
		ui::SetNextItemWidth(LeftWidth / 2.4f);
		ENABLE_DISABLE_WIDGET_IF(ui::BeginCombo("##UnrealVersion", unreal_versions[ue_selected_version].c_str()), game_ue_disabled,
		{
			for (size_t i = 0; i < unreal_versions.size(); ++i)
				if (ui::Selectable(unreal_versions[i].c_str())) ue_selected_version = i;

			ui::EndCombo();
		});
	}

	// Window Title
	{
		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Win Title  : ");
		ui::SameLine();

		if (process_id != NULL && Memory::IsValidProcess(process_id))
		{
			// Get Window Title
			if (!window_title.empty())
			{
				HWND window = FindWindow(UNREAL_WINDOW_CLASS, nullptr);

				if (window != INVALID_HANDLE_VALUE)
					GetWindowText(window, window_title.data(), 27);
			}
		}

		if (window_title[0] == '\0')
			window_title.replace(0, 4, "NONE");

		ui::Text("%s", window_title.c_str());
	}
}

void MemoryInterface(UiWindow* thiz)
{
	ui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
	if (ui::BeginChild("test", { 0, 210 }, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		mem_edit.DrawContents(nullptr, BufSize, CurrentViewerAddress);
		ui::EndChild();
	}
	ui::PopStyleColor();
}

void Finder(UiWindow* thiz)
{
	if (ui::BeginTabItem("Finder"))
	{
		if (cur_tap_id != 1)
		{
			//thiz.SetSize(380, 620);
			cur_tap_id = 1;
		}

		// ## GObjects
		{
			ui::BeginGroup();

			// Label
			ui::AlignTextToFramePadding();
			static float gobj_label_pos = ui::GetCursorPosX() + abs(ui::CalcTextSize("!~[ GObjects ]~!").x - (RightWidth / 2.3f)) / 2 - 5.f;
			ui::SetCursorPosX(gobj_label_pos);
			ui::TextColored(ImVec4(0.16f, 0.50f, 72.0f, 1.0f), "!~[ GObjects ]~!");

			// Finder
			ENABLE_DISABLE_WIDGET_IF(ui::Button("Find##GObjects", { RightWidth / 5.4f, 0.0f }), g_objects_find_disabled,
			{
				if (IsReadyToGo())
					ui::OpenPopup("Easy?");
				else
					popup_not_valid_process = true;
			});

			ui::SameLine();
			if (ui::Button("Use##Objects", { RightWidth / 5.4f, 0.0f }))
			{
				if (size_t(g_obj_listbox_item_current) < g_obj_listbox_items.size())
				{
					if (!g_objects_disabled)
						strcpy_s(g_objects_buf, sizeof g_objects_buf, g_obj_listbox_items[g_obj_listbox_item_current].data());

					if (Utils::MemoryObj)
					{
						uintptr_t address = Utils::CharArrayToUintptr(g_obj_listbox_items[g_obj_listbox_item_current]);

						// Only alloc once
						if (!PCurrentAddressData)
							PCurrentAddressData = new BYTE[BufSize];

						Utils::MemoryObj->ReadBytes(address, PCurrentAddressData, BufSize);
						GoToAddress(address);
						
					}
				}
			}

			ui::SetNextItemWidth(RightWidth / 2.5f);
			ui::ListBox("##Obj_listbox",
				&g_obj_listbox_item_current,
				VectorGetter,
				static_cast<void*>(&g_obj_listbox_items), static_cast<int>(g_obj_listbox_items.size()),
				3);

			// Popup
			if (ui::BeginPopupModal("Easy?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
			{
				ui::Text("First try EASY method. not work.?\nUse HARD method and wait some time.!\nUse Easy Method .?\n\n");
				ui::Separator();

				if (ui::Button("Yes", ImVec2(75, 0)))
				{
					ui::CloseCurrentPopup();
					StartGObjFinder(true);
				}

				ui::SetItemDefaultFocus();
				ui::SameLine();
				if (ui::Button("No", ImVec2(75, 0)))
				{
					ui::CloseCurrentPopup();
					StartGObjFinder(false);
				}

				ui::SameLine();
				if (ui::Button("Cancel", ImVec2(75, 0)))
					ui::CloseCurrentPopup();

				ui::EndPopup();
			}

			ui::EndGroup();
		}

		ui::SameLine();
		ui::VerticalSeparator();
		ui::SameLine();

		// ## GNames
		{
			ui::BeginGroup();
			ui::AlignTextToFramePadding();
			static float gnames_label_pos = RightWidth / 2 + (abs(ui::CalcTextSize("!~[ GNames ]~!").x - RightWidth / 2.3f) / 2.f) - 15.f;
			ui::SetCursorPosX(gnames_label_pos);
			ui::TextColored(ImVec4(0.16f, 0.50f, 72.0f, 1.0f), "!~[ GNames ]~!");

			// Start Finder
			ENABLE_DISABLE_WIDGET_IF(ui::Button("Find##GNames", { RightWidth / 5.4f, 0.0f }), g_names_find_disabled,
				{
					if (IsReadyToGo())
						StartGNamesFinder();
				else
					popup_not_valid_process = true;
			});

			ui::SameLine();

			// Set to input box
			if (ui::Button("Use##Names", { RightWidth / 5.4f, 0.0f }))
			{
				if (size_t(g_names_listbox_item_current) < g_names_listbox_items.size())
				{
					if (!g_names_disabled)
						strcpy_s(g_names_buf, sizeof g_names_buf, g_names_listbox_items[g_names_listbox_item_current].data());

					if (Utils::MemoryObj)
					{
						uintptr_t address = Utils::CharArrayToUintptr(g_names_listbox_items[g_names_listbox_item_current]);

						// Only alloc once
						if (!PCurrentAddressData)
							PCurrentAddressData = new BYTE[BufSize];

						Utils::MemoryObj->ReadBytes(address, PCurrentAddressData, BufSize);
						GoToAddress(address);
					}
				}
			}

			ui::SetNextItemWidth(RightWidth / 2.5f);
			ui::ListBox("##Names_listbox",
				&g_names_listbox_item_current,
				VectorGetter,
				static_cast<void*>(&g_names_listbox_items),
				static_cast<int>(g_names_listbox_items.size()),
				3);
			ui::EndGroup();
		}

		ui::Separator();

		// ## Class
		{
			ui::BeginGroup();

			// Label
			ui::AlignTextToFramePadding();
			static float class_label_pos = ui::GetCursorPosX() + abs(ui::CalcTextSize("!~[ Classes ]~!").x - RightWidth) / 2.f - 20.f;
			ui::SetCursorPosX(class_label_pos);
			ui::TextColored(ImVec4(0.16f, 0.50f, 72.0f, 1.0f), "!~[ Classes ]~!");

			ui::AlignTextToFramePadding();
			ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Class   :");
			ui::SameLine();

			// Class Input
			ui::SetNextItemWidth(RightWidth / 1.75f);
			ENABLE_DISABLE_WIDGET(ui::InputTextWithHint("##FindClass", "LocalPlayer, 0x0000000000", class_find_buf, IM_ARRAYSIZE(class_find_buf)), class_find_input_disabled);
			ui::SameLine();
			HelpMarker("What you can put here.?\n- Class Name:\n  - LocalPlayer or ULocalPlayer.\n  - MyGameInstance_C or UMyGameInstance_C.\n  - PlayerController or APlayerController.\n\n- Instance address:\n  - 0x0000000000.\n  - 0000000000.");

			ui::AlignTextToFramePadding();
			ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Buttons :");
			ui::SameLine();

			// Start Finder
			ENABLE_DISABLE_WIDGET_IF(ui::Button(" Find Class "), class_find_disabled,
			{
				if (IsReadyToGo())
					StartClassFinder();
				else
					popup_not_valid_process = true;
			});

			ui::SameLine();

			// Copy to clipboard
			if (ui::Button(" Copy Selected "))
			{
				if (size_t(class_listbox_item_current) < class_listbox_items.size())
					ui::SetClipboardText(class_listbox_items[class_listbox_item_current].c_str());
			}

			ui::SetNextItemWidth(RightWidth - 45.f);
			ui::ListBox("##Class_listbox",
				&class_listbox_item_current,
				VectorGetter,
				static_cast<void*>(&class_listbox_items),
				static_cast<int>(class_listbox_items.size()),
				7);
			ui::EndGroup();
		}

		ui::EndTabItem();
	}
}

void InstanceLogger(UiWindow* thiz)
{
	if (ui::BeginTabItem("Instance"))
	{
		if (cur_tap_id != 2)
		{
			//thiz.SetSize(380, 407);
			cur_tap_id = 2;
		}

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Objects Count : ");
		ui::SameLine();
		ui::Text("%d", il_objects_count);

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Names Count   : ");
		ui::SameLine();
		ui::Text("%d", il_names_count);

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "State         : ");
		ui::SameLine();
		ui::Text("%s", il_state.c_str());

		// Start Logger
		ENABLE_DISABLE_WIDGET_IF(ui::Button("Start##InstanceLogger", { RightWidth - 45.f, 0.0f }), il_start_disabled,
		{
			if (IsReadyToGo())
				StartInstanceLogger();
			else
				popup_not_valid_process = true;
		});

		ui::EndTabItem();
	}
}

void SdkGenerator(UiWindow* thiz)
{
	if (ui::BeginTabItem("S-D-K"))
	{
		if (cur_tap_id != 3)
		{
			//thiz.SetSize(380, 622);
			cur_tap_id = 3;
		}

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Objects/Names : ");
		ui::SameLine();
		ui::Text("%d / %d", sg_objects_count, sg_names_count);

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Packages      : ");
		ui::SameLine();
		ui::Text("%d / %d", sg_packages_done_count, sg_packages_count);

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Sdk Type      : ");
		ui::SameLine();
		ui::SetNextItemWidth(100);
		ENABLE_DISABLE_WIDGET(ui::Combo("##SdkType", &sg_type_item_current, VectorGetter, static_cast<void*>(&sg_type_items), static_cast<int>(sg_type_items.size()), 4), sg_type_disabled);
		ui::SameLine();
		HelpMarker("- Internal: Generate functions for class/struct.\n- External: Don't gen functions for class/struct,\n    But generate ReadAsMe for every class/struct.");

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Game Name     : ");
		ui::SameLine();
		ui::SetNextItemWidth(RightWidth / 1.9f);
		ENABLE_DISABLE_WIDGET(ui::InputTextWithHint("##GameName", "PUBG, Fortnite", sg_game_name_buf, IM_ARRAYSIZE(sg_game_name_buf)), sg_game_name_disabled);

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Game Version  : ");
		ui::SameLine();
		ui::SetNextItemWidth(RightWidth / 1.9f);
		ENABLE_DISABLE_WIDGET(ui::InputInt3("##GameVersion", sg_game_version), sg_game_version_disabled);

		ui::AlignTextToFramePadding();
		ui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "State         : ");
		ui::SameLine();
		ui::Text("%s", sg_state.c_str());

		// Packages Box
		ui::SetNextItemWidth(RightWidth - 45.f);
		ui::ListBox("##Packages_listbox",
			&sg_packages_item_current,
			VectorGetter,
			static_cast<void*>(&sg_packages_items),
			static_cast<int>(sg_packages_items.size()), 8);

		// Start Generator
		ENABLE_DISABLE_WIDGET_IF(ui::Button("Start##SdkGenerator", { RightWidth - 45.f, 0.0f }), sg_start_disabled,
		{
			if (IsReadyToGo())
				StartSdkGenerator();
			else
				popup_not_valid_process = true;
		});

		ui::EndTabItem();
	}
}

void MainUi(UiWindow* thiz)
{
	TitleBar(thiz);

	ui::Separator();

	// left-group
	{
		ui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
		if (ui::BeginChild("##left-group", { 320.f - thiz->GetUiStyle().ItemSpacing.x, 0 }, false))
		{
			LeftWidth = ui::GetWindowWidth();

			InformationSection(thiz);
			ui::Separator();

			// Tabs
			{
				if (ui::BeginTabBar("ToolsTabBar", ImGuiTabBarFlags_NoTooltip))
				{
					if (ui::BeginTabItem("Address Viewer"))
					{
						MemoryInterface(thiz);
						ui::EndTabItem();
					}

					ui::EndTabBar();
				}
			}

			ui::EndChild();
		}
		ui::PopStyleColor();

		ui::SameLine();
		ui::VerticalSeparator();
		ui::SameLine();
	}
	
	// right-group
	{
		ui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
		if (ui::BeginChild("##right-group", {Utils::UiMainWindow->GetSize().x - LeftWidth - thiz->GetUiStyle().ItemSpacing.x, 0 }, false))
		{
			RightWidth = ui::GetWindowWidth();

			// Tabs
			{
				if (ui::BeginTabBar("Debug", ImGuiTabBarFlags_NoTooltip))
				{
					Finder(thiz);
					InstanceLogger(thiz);
					SdkGenerator(thiz);

					ui::EndTabBar();
				}
			}

			ui::EndChild();
		}
		ui::PopStyleColor();
	}

	// Popups
	{
		WarningPopup("Note", "Sdk Generator finished. !!", sg_finished);
		WarningPopup("Warning", "Not Valid Process ID. !!", popup_not_valid_process);
		WarningPopup("Warning", "Not Valid GNames Address. !!", popup_not_valid_gnames);
		WarningPopup("Warning", "Not Valid GObjects Address. !!", popup_not_valid_gobjects);
	}

}
#pragma endregion

// int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);
// Fix vs2019 Problem [wWinMain instead of WinMain]
// ReSharper disable once CppInconsistentNaming
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd) // NOLINT(readability-non-const-parameter)
{
	// Remove unneeded variables
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nShowCmd);

	// Load Settings / Json Core
	if (!Utils::LoadSettings()) return 0;
	if (!Utils::LoadEngineCore(unreal_versions)) return 0;

	// Autodetect in case game already open
	///////////////////////////////////////////////////////////process_id = Utils::DetectUnrealGameId();

	// Setup Address Viewer
	mem_edit.Cols = 8;
	mem_edit.OptMidColsCount = 4;
	mem_edit.OptShowAscii = false;
	mem_edit.OptShowHexIi = false;
	mem_edit.OptShowOptions = false;
	mem_edit.OptShowDataPreview = true;
	mem_edit.OptShowDataPreviewAs = false;
	mem_edit.OptShowDataPreviewDec = false;
	mem_edit.OptShowDataPreviewBin = false;
	mem_edit.OptShowDataPreviewHex = true;
	mem_edit.HighlightColor = IM_COL32(0, 0, 200, 200);
	mem_edit.ReadOnly = true;
	mem_edit.ReadFn = &AddressViewerReadFn;

	// Run the new debugging tools
	Debugging d;
	d.EnterDebugMode();

	// Launch the main window
	Utils::UiMainWindow = new UiWindow("Unreal Finder Tool. Version: " TOOL_VERSION, "CorrMFinder", 680, 530);
	Utils::UiMainWindow->Show(MainUi);

	while (!Utils::UiMainWindow->Closed())
		Sleep(1);

	// Cleanup
	if (Utils::MemoryObj)
	{
		/////////////////////////////////////////////Utils::MemoryObj->ResumeProcess();
		CloseHandle(Utils::MemoryObj->ProcessHandle);
		delete Utils::MemoryObj;
	}

	return ERROR_SUCCESS;
}
