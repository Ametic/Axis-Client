/* Copyright © 2026 BestProject Team */
#include <base/fs.h>
#include <base/io.h>
#include <base/math.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace FontIcon;

namespace
{
	static constexpr const char *AXIS_SHOP_HOST = "https://teeworlds.xyz";
	static constexpr const char *AXIS_SHOP_BROWSE_API_URL = "https://teeworlds.xyz/api/skins?page=%d&limit=10&type=%s";
	static constexpr const char *AXIS_SHOP_SEARCH_API_URL = "https://teeworlds.xyz/api/skins?page=%d&limit=10&type=%s&search=%s";
	static constexpr CTimeout AXIS_SHOP_TIMEOUT{8000, 0, 1024, 8};
	static constexpr int64_t AXIS_SHOP_PAGE_MAX_RESPONSE_SIZE = 2 * 1024 * 1024;
	static constexpr int64_t AXIS_SHOP_IMAGE_MAX_RESPONSE_SIZE = 32 * 1024 * 1024;
	static constexpr int64_t AXIS_SHOP_AUDIO_MAX_RESPONSE_SIZE = 128 * 1024 * 1024;

	static constexpr float AXIS_SHOP_MARGIN = 10.0f;
	static constexpr float AXIS_SHOP_MARGIN_SMALL = 5.0f;
	static constexpr float AXIS_SHOP_LINE_SIZE = 20.0f;
	static constexpr float AXIS_SHOP_HEADLINE_FONT_SIZE = 20.0f;
	static constexpr float AXIS_SHOP_FONT_SIZE = 14.0f;
	static constexpr float AXIS_SHOP_SMALL_FONT_SIZE = 12.0f;
	static constexpr float AXIS_SHOP_TAB_WIDTH = 120.0f;
	static constexpr float AXIS_SHOP_TAB_HEIGHT = 26.0f;
	static constexpr float AXIS_SHOP_SECTION_ROUNDING = 8.0f;
	static constexpr float AXIS_SHOP_ITEM_HEIGHT = 88.0f;

	enum
	{
		AXIS_SHOP_ENTITIES = 0,
		AXIS_SHOP_GAME,
		AXIS_SHOP_EMOTICONS,
		AXIS_SHOP_PARTICLES,
		AXIS_SHOP_HUD,
		AXIS_SHOP_ARROWS,
		AXIS_SHOP_CURSORS,
		AXIS_SHOP_AUDIO,
		NUM_AXIS_SHOP_TABS,
	};

	enum
	{
		AXIS_ASSETS_TAB_ENTITIES = 0,
		AXIS_ASSETS_TAB_GAME = 1,
		AXIS_ASSETS_TAB_EMOTICONS = 2,
		AXIS_ASSETS_TAB_PARTICLES = 3,
		AXIS_ASSETS_TAB_HUD = 4,
		AXIS_ASSETS_TAB_CURSOR = 6,
		AXIS_ASSETS_TAB_ARROW = 7,
		AXIS_ASSETS_TAB_AUDIO = 8,
	};

	struct SAxisClientShopTypeInfo
	{
		const char *m_pLabel;
		const char *m_pApiType;
		const char *m_pAssetDirectory;
		int m_AssetsTab;
	};

	static const SAxisClientShopTypeInfo gs_aAxisClientShopTypeInfos[NUM_AXIS_SHOP_TABS] = {
		{"Entities", "entity", "assets/entities", AXIS_ASSETS_TAB_ENTITIES},
		{"Game", "gameskin", "assets/game", AXIS_ASSETS_TAB_GAME},
		{"Emoticons", "emoticon", "assets/emoticons", AXIS_ASSETS_TAB_EMOTICONS},
		{"Particles", "particle", "assets/particles", AXIS_ASSETS_TAB_PARTICLES},
		{"HUD", "hud", "assets/hud", AXIS_ASSETS_TAB_HUD},
		{"Arrows", "arrows", "assets/arrow", AXIS_ASSETS_TAB_ARROW},
		{"Cursors", "cursor", "assets/cursor", AXIS_ASSETS_TAB_CURSOR},
		{"Audio", "sounds", "assets/audio", AXIS_ASSETS_TAB_AUDIO},
	};

	struct SAxisClientShopItem
	{
		char m_aId[64]{};
		char m_aName[128]{};
		char m_aFilename[128]{};
		char m_aUsername[64]{};
		char m_aImageUrl[256]{};
		char m_aDownloadUrl[256]{};
		bool m_PreviewFailed = false;
		int m_PreviewWidth = 0;
		int m_PreviewHeight = 0;
		IGraphics::CTextureHandle m_PreviewTexture;
		CButtonContainer m_PreviewButton;
		CButtonContainer m_ActionButton;
		CButtonContainer m_DeleteButton;
	};

	struct SAxisClientShopState
	{
		bool m_Initialized = false;
		int m_Tab = AXIS_SHOP_ENTITIES;
		int m_SelectedIndex = -1;
		int m_TotalPages = 1;
		int m_TotalItems = 0;
		int m_LoadedTab = -1;
		int m_LoadedPage = 0;
		int m_FetchTab = -1;
		int m_FetchPage = 0;
		int m_PreviewTab = -1;
		int m_PreviewPage = 0;
		int m_InstallTab = -1;
		int m_InstallUrlIndex = 0;
		std::array<int, NUM_AXIS_SHOP_TABS> m_aPages{};
		std::shared_ptr<CHttpRequest> m_pFetchTask;
		std::shared_ptr<CHttpRequest> m_pInstallTask;
		std::shared_ptr<CHttpRequest> m_pPreviewTask;
		std::vector<SAxisClientShopItem> m_vItems;
		std::vector<std::string> m_vInstallUrls;
		char m_aAppliedSearch[128]{};
		char m_aLoadedSearch[128]{};
		char m_aFetchSearch[128]{};
		char m_aPreviewSearch[128]{};
		char m_aInstallAssetName[128]{};
		char m_aInstallItemId[64]{};
		char m_aPreviewItemId[64]{};
		char m_aPreviewPath[IO_MAX_PATH_LENGTH]{};
		char m_aStatus[256]{};
		char m_aOpenPreviewItemId[64]{};
		bool m_PreviewOpen = false;
		bool m_Visible = false;
		CButtonContainer m_PreviewCloseButton;
	};

	static SAxisClientShopState gs_AxisClientShopState;
	static CLineInputBuffered<128> gs_AxisClientShopSearchInput;

	static void AxisClientShopSetStatus(const char *pText)
	{
		str_copy(gs_AxisClientShopState.m_aStatus, pText, sizeof(gs_AxisClientShopState.m_aStatus));
	}

	static void AxisClientShopAbortTask(std::shared_ptr<CHttpRequest> &pTask)
	{
		if(pTask)
		{
			pTask->Abort();
			pTask = nullptr;
		}
	}

	static void AxisClientShopResetInstallState()
	{
		gs_AxisClientShopState.m_vInstallUrls.clear();
		gs_AxisClientShopState.m_InstallUrlIndex = 0;
		gs_AxisClientShopState.m_InstallTab = -1;
		gs_AxisClientShopState.m_aInstallAssetName[0] = '\0';
		gs_AxisClientShopState.m_aInstallItemId[0] = '\0';
	}

	static void AxisClientShopCancelInstall()
	{
		AxisClientShopAbortTask(gs_AxisClientShopState.m_pInstallTask);
		AxisClientShopResetInstallState();
		AxisClientShopSetStatus(Localize("Download canceled"));
	}

	static void AxisClientShopInitState()
	{
		if(gs_AxisClientShopState.m_Initialized)
		{
			return;
		}

		gs_AxisClientShopState.m_Initialized = true;
		gs_AxisClientShopState.m_aPages.fill(1);
		gs_AxisClientShopState.m_TotalPages = 1;
	}

	static void AxisClientShopClosePreview()
	{
		gs_AxisClientShopState.m_PreviewOpen = false;
		gs_AxisClientShopState.m_aOpenPreviewItemId[0] = '\0';
	}

	static bool AxisClientShopHasPreviewOpen()
	{
		return gs_AxisClientShopState.m_PreviewOpen && gs_AxisClientShopState.m_aOpenPreviewItemId[0] != '\0';
	}

	static bool AxisClientShopHasActiveSearch()
	{
		return gs_AxisClientShopState.m_aAppliedSearch[0] != '\0';
	}

	static void AxisClientShopClearItems(CMenus *pMenus)
	{
		for(SAxisClientShopItem &Item : gs_AxisClientShopState.m_vItems)
		{
			if(Item.m_PreviewTexture.IsValid())
			{
				pMenus->MenuGraphics()->UnloadTexture(&Item.m_PreviewTexture);
			}
		}
		gs_AxisClientShopState.m_vItems.clear();
		gs_AxisClientShopState.m_SelectedIndex = -1;
	}

	static void AxisClientShopAbortPreviewTask()
	{
		AxisClientShopAbortTask(gs_AxisClientShopState.m_pPreviewTask);
		gs_AxisClientShopState.m_PreviewTab = -1;
		gs_AxisClientShopState.m_PreviewPage = 0;
		gs_AxisClientShopState.m_aPreviewItemId[0] = '\0';
		gs_AxisClientShopState.m_aPreviewPath[0] = '\0';
		gs_AxisClientShopState.m_aPreviewSearch[0] = '\0';
	}

	static void AxisClientShopInvalidatePage(CMenus *pMenus)
	{
		AxisClientShopAbortTask(gs_AxisClientShopState.m_pFetchTask);
		AxisClientShopAbortPreviewTask();
		AxisClientShopClosePreview();
		gs_AxisClientShopState.m_LoadedTab = -1;
		gs_AxisClientShopState.m_LoadedPage = 0;
		gs_AxisClientShopState.m_aLoadedSearch[0] = '\0';
	}

	static void AxisClientShopTrimQuery(const char *pInput, char *pOutput, size_t OutputSize)
	{
		str_copy(pOutput, pInput != nullptr ? pInput : "", OutputSize);

		int Start = 0;
		while(pOutput[Start] != '\0' && (unsigned char)pOutput[Start] <= 32)
		{
			++Start;
		}

		int End = str_length(pOutput);
		const int OriginalEnd = End;
		while(End > Start && (unsigned char)pOutput[End - 1] <= 32)
		{
			--End;
		}

		if(Start > 0 || End < OriginalEnd)
		{
			int WritePos = 0;
			for(int ReadPos = Start; ReadPos < End && WritePos < (int)OutputSize - 1; ++ReadPos)
			{
				pOutput[WritePos++] = pOutput[ReadPos];
			}
			pOutput[WritePos] = '\0';
		}
	}

	static void AxisClientShopSetTab(CMenus *pMenus, int Tab)
	{
		AxisClientShopInitState();
		Tab = std::clamp(Tab, 0, NUM_AXIS_SHOP_TABS - 1);
		if(gs_AxisClientShopState.m_Tab == Tab)
		{
			return;
		}

		gs_AxisClientShopState.m_Tab = Tab;
		AxisClientShopInvalidatePage(pMenus);
	}

	static void AxisClientShopSetPage(CMenus *pMenus, int Page)
	{
		Page = maximum(1, Page);
		int &CurrentPage = gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab];
		if(CurrentPage == Page)
		{
			return;
		}

		CurrentPage = Page;
		AxisClientShopInvalidatePage(pMenus);
	}

	static void AxisClientShopSetSearch(CMenus *pMenus, const char *pSearch)
	{
		char aTrimmed[128];
		AxisClientShopTrimQuery(pSearch, aTrimmed, sizeof(aTrimmed));
		if(str_comp(aTrimmed, gs_AxisClientShopState.m_aAppliedSearch) == 0)
		{
			return;
		}

		str_copy(gs_AxisClientShopState.m_aAppliedSearch, aTrimmed, sizeof(gs_AxisClientShopState.m_aAppliedSearch));
		AxisClientShopInvalidatePage(pMenus);
	}

	static void AxisClientShopResolveUrl(const char *pInput, char *pOutput, size_t OutputSize)
	{
		pOutput[0] = '\0';
		if(pInput == nullptr || pInput[0] == '\0')
		{
			return;
		}

		if(str_startswith(pInput, "https://") != nullptr || str_startswith(pInput, "http://") != nullptr)
		{
			str_copy(pOutput, pInput, OutputSize);
		}
		else if(pInput[0] == '/')
		{
			str_format(pOutput, OutputSize, "%s%s", AXIS_SHOP_HOST, pInput);
		}
		else
		{
			str_format(pOutput, OutputSize, "%s/%s", AXIS_SHOP_HOST, pInput);
		}
	}

	static void AxisClientShopNormalizeAssetName(const char *pName, const char *pFilename, char *pOutput, size_t OutputSize)
	{
		char aRawName[128];
		if(pFilename != nullptr && pFilename[0] != '\0')
		{
			IStorage::StripPathAndExtension(pFilename, aRawName, sizeof(aRawName));
		}
		else if(pName != nullptr && pName[0] != '\0')
		{
			str_copy(aRawName, pName, sizeof(aRawName));
		}
		else
		{
			str_copy(aRawName, "asset", sizeof(aRawName));
		}

		str_sanitize_filename(aRawName);

		char aSanitized[128];
		int WritePos = 0;
		bool LastWasSeparator = true;
		for(int ReadPos = 0; aRawName[ReadPos] != '\0' && WritePos < (int)sizeof(aSanitized) - 1; ++ReadPos)
		{
			unsigned char Character = (unsigned char)aRawName[ReadPos];
			if(Character <= 32)
			{
				if(!LastWasSeparator)
				{
					aSanitized[WritePos++] = '_';
					LastWasSeparator = true;
				}
				continue;
			}

			aSanitized[WritePos++] = Character;
			LastWasSeparator = false;
		}
		aSanitized[WritePos] = '\0';

		while(WritePos > 0 && aSanitized[WritePos - 1] == '_')
		{
			aSanitized[--WritePos] = '\0';
		}

		if(aSanitized[0] == '\0')
		{
			str_copy(aSanitized, "asset", sizeof(aSanitized));
		}

		str_copy(pOutput, aSanitized, OutputSize);
	}

	static void AxisClientShopBuildPreviewPath(int Tab, const char *pItemId, char *pOutput, size_t OutputSize)
	{
		str_format(pOutput, OutputSize, "axisclient/shop_previews/%s/%s.png", gs_aAxisClientShopTypeInfos[Tab].m_pApiType, pItemId);
	}

	static void AxisClientShopBuildAssetPath(int Tab, const char *pAssetName, char *pOutput, size_t OutputSize)
	{
		if(Tab == AXIS_SHOP_AUDIO)
		{
			str_format(pOutput, OutputSize, "%s/%s", gs_aAxisClientShopTypeInfos[Tab].m_pAssetDirectory, pAssetName);
		}
		else
		{
			str_format(pOutput, OutputSize, "%s/%s.png", gs_aAxisClientShopTypeInfos[Tab].m_pAssetDirectory, pAssetName);
		}
	}

	static void AxisClientShopBuildAssetDirectoryPath(int Tab, const char *pAssetName, char *pOutput, size_t OutputSize)
	{
		str_format(pOutput, OutputSize, "%s/%s", gs_aAxisClientShopTypeInfos[Tab].m_pAssetDirectory, pAssetName);
	}

	static bool AxisClientShopWriteFile(CMenus *pMenus, const char *pRelativePath, const void *pData, size_t DataSize)
	{
		char aAbsolutePath[IO_MAX_PATH_LENGTH];
		pMenus->MenuStorage()->GetCompletePath(IStorage::TYPE_SAVE, pRelativePath, aAbsolutePath, sizeof(aAbsolutePath));
		if(fs_makedir_rec_for(aAbsolutePath) != 0)
		{
			return false;
		}

		IOHANDLE File = pMenus->MenuStorage()->OpenFile(pRelativePath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File == nullptr)
		{
			return false;
		}

		const bool Success = io_write(File, pData, DataSize) == DataSize && io_error(File) == 0 && io_close(File) == 0;
		return Success;
	}

	static bool AxisClientShopIsPngBuffer(const unsigned char *pData, size_t DataSize)
	{
		static const unsigned char s_aSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		return DataSize >= sizeof(s_aSignature) && mem_comp(pData, s_aSignature, sizeof(s_aSignature)) == 0;
	}

	static bool AxisClientShopIsZipBuffer(const unsigned char *pData, size_t DataSize)
	{
		static const unsigned char s_aSignature[4] = {'P', 'K', 0x03, 0x04};
		return DataSize >= sizeof(s_aSignature) && mem_comp(pData, s_aSignature, sizeof(s_aSignature)) == 0;
	}

	static bool AxisClientShopDirectoryExists(CMenus *pMenus, const char *pRelativePath)
	{
		char aAbsolutePath[IO_MAX_PATH_LENGTH];
		pMenus->MenuStorage()->GetCompletePath(IStorage::TYPE_SAVE, pRelativePath, aAbsolutePath, sizeof(aAbsolutePath));
		return fs_is_dir(aAbsolutePath) == 1;
	}

	static int AxisClientShopCollectDirectoryEntries(const char *pName, int IsDir, int DirType, void *pUser)
	{
		(void)IsDir;
		(void)DirType;
		auto *pEntries = static_cast<std::vector<std::string> *>(pUser);
		if(str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0)
		{
			return 0;
		}
		pEntries->emplace_back(pName);
		return 0;
	}

	static bool AxisClientShopRemoveAbsoluteDirectoryRecursive(const char *pAbsolutePath)
	{
		std::vector<std::string> vEntries;
		fs_listdir(pAbsolutePath, AxisClientShopCollectDirectoryEntries, 0, &vEntries);

		bool Success = true;
		for(const std::string &Entry : vEntries)
		{
			char aChildPath[IO_MAX_PATH_LENGTH];
			str_format(aChildPath, sizeof(aChildPath), "%s/%s", pAbsolutePath, Entry.c_str());
			if(fs_is_dir(aChildPath) == 1)
			{
				Success &= AxisClientShopRemoveAbsoluteDirectoryRecursive(aChildPath);
			}
			else
			{
				Success &= fs_remove(aChildPath) == 0;
			}
		}

		Success &= fs_removedir(pAbsolutePath) == 0;
		return Success;
	}

	static uint16_t AxisClientShopReadLe16(const unsigned char *pData)
	{
		return (uint16_t)pData[0] | ((uint16_t)pData[1] << 8);
	}

	static uint32_t AxisClientShopReadLe32(const unsigned char *pData)
	{
		return (uint32_t)pData[0] | ((uint32_t)pData[1] << 8) | ((uint32_t)pData[2] << 16) | ((uint32_t)pData[3] << 24);
	}

	static bool AxisClientShopSanitizeArchivePath(const std::string &Path, std::string &OutPath)
	{
		OutPath.clear();
		if(Path.empty())
		{
			return false;
		}

		OutPath.reserve(Path.size());
		for(char Ch : Path)
		{
			OutPath.push_back(Ch == '\\' ? '/' : Ch);
		}

		while(!OutPath.empty() && OutPath[0] == '/')
		{
			OutPath.erase(OutPath.begin());
		}

		if(OutPath.empty())
		{
			return false;
		}

		if((OutPath.size() >= 2 && OutPath[1] == ':') || str_startswith(OutPath.c_str(), "../") || str_find(OutPath.c_str(), "/../") != nullptr || str_endswith(OutPath.c_str(), "/.."))
		{
			return false;
		}

		return true;
	}

	static bool AxisClientShopExtractZipToDirectory(CMenus *pMenus, const unsigned char *pData, size_t DataSize, const char *pOutputBasePath)
	{
		if(!AxisClientShopIsZipBuffer(pData, DataSize) || DataSize < 22)
		{
			return false;
		}

		struct SZipEntry
		{
			std::string m_Path;
			uint32_t m_LocalHeaderOffset = 0;
			uint32_t m_CompressedSize = 0;
			uint32_t m_UncompressedSize = 0;
			uint16_t m_CompressionMethod = 0;
		};

		size_t EocdOffset = SIZE_MAX;
		const size_t SearchStart = DataSize > 65557 ? DataSize - 65557 : 0;
		for(size_t Pos = DataSize - 22 + 1; Pos-- > SearchStart;)
		{
			if(Pos + 4 <= DataSize && AxisClientShopReadLe32(pData + Pos) == 0x06054b50U)
			{
				EocdOffset = Pos;
				break;
			}
		}

		if(EocdOffset == SIZE_MAX || EocdOffset + 22 > DataSize)
		{
			return false;
		}

		const uint16_t NumEntries = AxisClientShopReadLe16(pData + EocdOffset + 10);
		const uint32_t CentralDirSize = AxisClientShopReadLe32(pData + EocdOffset + 12);
		const uint32_t CentralDirOffset = AxisClientShopReadLe32(pData + EocdOffset + 16);
		if((size_t)CentralDirOffset + (size_t)CentralDirSize > DataSize)
		{
			return false;
		}

		std::vector<SZipEntry> vEntries;
		vEntries.reserve(NumEntries);

		size_t CentralPos = CentralDirOffset;
		for(uint16_t EntryIndex = 0; EntryIndex < NumEntries; ++EntryIndex)
		{
			if(CentralPos + 46 > DataSize || AxisClientShopReadLe32(pData + CentralPos) != 0x02014b50U)
			{
				return false;
			}

			const uint16_t CompressionMethod = AxisClientShopReadLe16(pData + CentralPos + 10);
			const uint32_t CompressedSize = AxisClientShopReadLe32(pData + CentralPos + 20);
			const uint32_t UncompressedSize = AxisClientShopReadLe32(pData + CentralPos + 24);
			const uint16_t NameLength = AxisClientShopReadLe16(pData + CentralPos + 28);
			const uint16_t ExtraLength = AxisClientShopReadLe16(pData + CentralPos + 30);
			const uint16_t CommentLength = AxisClientShopReadLe16(pData + CentralPos + 32);
			const uint32_t LocalHeaderOffset = AxisClientShopReadLe32(pData + CentralPos + 42);

			const size_t NameOffset = CentralPos + 46;
			const size_t NextEntryPos = NameOffset + NameLength + ExtraLength + CommentLength;
			if(NameOffset + NameLength > DataSize || NextEntryPos > DataSize)
			{
				return false;
			}

			std::string RawPath((const char *)(pData + NameOffset), NameLength);
			std::string SanitizedPath;
			if(!RawPath.empty() && RawPath.back() != '/')
			{
				if(!AxisClientShopSanitizeArchivePath(RawPath, SanitizedPath))
				{
					return false;
				}
				vEntries.push_back({SanitizedPath, LocalHeaderOffset, CompressedSize, UncompressedSize, CompressionMethod});
			}

			CentralPos = NextEntryPos;
		}

		if(vEntries.empty())
		{
			return false;
		}

		std::string CommonPrefix;
		{
			std::string Candidate;
			bool CandidateReady = false;
			bool PrefixMismatch = false;
			for(const SZipEntry &Entry : vEntries)
			{
				const size_t SlashPos = Entry.m_Path.find('/');
				if(SlashPos == std::string::npos || SlashPos == 0 || SlashPos + 1 >= Entry.m_Path.size())
				{
					PrefixMismatch = true;
					break;
				}

				const std::string Prefix = Entry.m_Path.substr(0, SlashPos);
				if(!CandidateReady)
				{
					Candidate = Prefix;
					CandidateReady = true;
				}
				else if(Candidate != Prefix)
				{
					PrefixMismatch = true;
					break;
				}
			}

			if(CandidateReady && !PrefixMismatch)
			{
				CommonPrefix = Candidate + "/";
			}
		}

		for(const SZipEntry &Entry : vEntries)
		{
			std::string RelativePath = Entry.m_Path;
			if(!CommonPrefix.empty() && str_startswith(RelativePath.c_str(), CommonPrefix.c_str()) != nullptr)
			{
				RelativePath = RelativePath.substr(CommonPrefix.size());
			}
			if(RelativePath.empty())
			{
				continue;
			}

			const size_t LocalPos = Entry.m_LocalHeaderOffset;
			if(LocalPos + 30 > DataSize || AxisClientShopReadLe32(pData + LocalPos) != 0x04034b50U)
			{
				return false;
			}

			const uint16_t LocalNameLength = AxisClientShopReadLe16(pData + LocalPos + 26);
			const uint16_t LocalExtraLength = AxisClientShopReadLe16(pData + LocalPos + 28);
			const size_t CompressedOffset = LocalPos + 30 + LocalNameLength + LocalExtraLength;
			if(CompressedOffset + (size_t)Entry.m_CompressedSize > DataSize)
			{
				return false;
			}

			const unsigned char *pCompressedData = pData + CompressedOffset;
			std::vector<unsigned char> vFileData;
			if(Entry.m_CompressionMethod == 0)
			{
				vFileData.assign(pCompressedData, pCompressedData + Entry.m_CompressedSize);
			}
			else if(Entry.m_CompressionMethod == 8)
			{
				if(Entry.m_UncompressedSize == 0)
				{
					vFileData.clear();
				}
				else
				{
					vFileData.resize(Entry.m_UncompressedSize);
					z_stream Stream = {};
					Stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(pCompressedData));
					Stream.avail_in = Entry.m_CompressedSize;
					Stream.next_out = reinterpret_cast<Bytef *>(vFileData.data());
					Stream.avail_out = Entry.m_UncompressedSize;

					if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
					{
						return false;
					}
					const int InflateResult = inflate(&Stream, Z_FINISH);
					inflateEnd(&Stream);
					if(InflateResult != Z_STREAM_END || Stream.total_out != Entry.m_UncompressedSize)
					{
						return false;
					}
				}
			}
			else
			{
				return false;
			}

			char aOutputPath[IO_MAX_PATH_LENGTH];
			str_format(aOutputPath, sizeof(aOutputPath), "%s/%s", pOutputBasePath, RelativePath.c_str());
			if(!AxisClientShopWriteFile(pMenus, aOutputPath, vFileData.data(), vFileData.size()))
			{
				return false;
			}
		}

		return true;
	}

	static bool AxisClientShopAssetExists(CMenus *pMenus, int Tab, const char *pAssetName)
	{
		if(Tab == AXIS_SHOP_AUDIO)
		{
			char aDirectoryPath[IO_MAX_PATH_LENGTH];
			AxisClientShopBuildAssetDirectoryPath(Tab, pAssetName, aDirectoryPath, sizeof(aDirectoryPath));
			if(AxisClientShopDirectoryExists(pMenus, aDirectoryPath))
			{
				return true;
			}
			str_format(aDirectoryPath, sizeof(aDirectoryPath), "audio/%s", pAssetName);
			return AxisClientShopDirectoryExists(pMenus, aDirectoryPath);
		}

		if(Tab == AXIS_SHOP_ARROWS)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "assets/arrow/%s.png", pAssetName);
			if(pMenus->MenuStorage()->FileExists(aPath, IStorage::TYPE_ALL))
			{
				return true;
			}
			str_format(aPath, sizeof(aPath), "assets/arrow/%s/arrow.png", pAssetName);
			if(pMenus->MenuStorage()->FileExists(aPath, IStorage::TYPE_ALL))
			{
				return true;
			}
			str_format(aPath, sizeof(aPath), "assets/arrows/%s.png", pAssetName);
			if(pMenus->MenuStorage()->FileExists(aPath, IStorage::TYPE_ALL))
			{
				return true;
			}
			str_format(aPath, sizeof(aPath), "assets/arrows/%s/arrow.png", pAssetName);
			return pMenus->MenuStorage()->FileExists(aPath, IStorage::TYPE_ALL);
		}

		char aPath[IO_MAX_PATH_LENGTH];
		AxisClientShopBuildAssetPath(Tab, pAssetName, aPath, sizeof(aPath));
		if(pMenus->MenuStorage()->FileExists(aPath, IStorage::TYPE_ALL))
		{
			return true;
		}

		char aDirectoryPath[IO_MAX_PATH_LENGTH];
		AxisClientShopBuildAssetDirectoryPath(Tab, pAssetName, aDirectoryPath, sizeof(aDirectoryPath));
		return AxisClientShopDirectoryExists(pMenus, aDirectoryPath);
	}

	static bool AxisClientShopAssetSelected(int Tab, const char *pAssetName)
	{
		switch(Tab)
		{
		case AXIS_SHOP_ENTITIES:
			return str_comp(g_Config.m_ClAssetsEntities, pAssetName) == 0;
		case AXIS_SHOP_GAME:
			return str_comp(g_Config.m_ClAssetGame, pAssetName) == 0;
		case AXIS_SHOP_EMOTICONS:
			return str_comp(g_Config.m_ClAssetEmoticons, pAssetName) == 0;
		case AXIS_SHOP_PARTICLES:
			return str_comp(g_Config.m_ClAssetParticles, pAssetName) == 0;
		case AXIS_SHOP_HUD:
			return str_comp(g_Config.m_ClAssetHud, pAssetName) == 0;
		case AXIS_SHOP_ARROWS:
			return str_comp(g_Config.m_ClAssetArrow, pAssetName) == 0;
		case AXIS_SHOP_CURSORS:
			return str_comp(g_Config.m_ClAssetCursor, pAssetName) == 0;
		case AXIS_SHOP_AUDIO:
			return str_comp(g_Config.m_SndPack, pAssetName) == 0;
		default:
			return false;
		}
	}

	static void AxisClientShopReloadAsset(CMenus *pMenus, int Tab, const char *pAssetName)
	{
		switch(Tab)
		{
		case AXIS_SHOP_ENTITIES:
			pMenus->MenuGameClient()->m_MapImages.ChangeEntitiesPath(pAssetName);
			break;
		case AXIS_SHOP_GAME:
			pMenus->MenuGameClient()->LoadGameSkin(pAssetName);
			break;
		case AXIS_SHOP_EMOTICONS:
			pMenus->MenuGameClient()->LoadEmoticonsSkin(pAssetName);
			break;
		case AXIS_SHOP_PARTICLES:
			pMenus->MenuGameClient()->LoadParticlesSkin(pAssetName);
			break;
		case AXIS_SHOP_HUD:
			pMenus->MenuGameClient()->LoadHudSkin(pAssetName);
			break;
		case AXIS_SHOP_ARROWS:
			pMenus->MenuGameClient()->LoadArrowAsset(pAssetName);
			break;
		case AXIS_SHOP_CURSORS:
			pMenus->MenuGameClient()->LoadCursorAsset(pAssetName);
			break;
		case AXIS_SHOP_AUDIO:
			pMenus->MenuGameClient()->m_Sounds.Clear();
			break;
		}
	}

	static void AxisClientShopApplyAsset(CMenus *pMenus, int Tab, const char *pAssetName, bool RefreshAssetList)
	{
		switch(Tab)
		{
		case AXIS_SHOP_ENTITIES:
			str_copy(g_Config.m_ClAssetsEntities, pAssetName);
			break;
		case AXIS_SHOP_GAME:
			str_copy(g_Config.m_ClAssetGame, pAssetName);
			break;
		case AXIS_SHOP_EMOTICONS:
			str_copy(g_Config.m_ClAssetEmoticons, pAssetName);
			break;
		case AXIS_SHOP_PARTICLES:
			str_copy(g_Config.m_ClAssetParticles, pAssetName);
			break;
		case AXIS_SHOP_HUD:
			str_copy(g_Config.m_ClAssetHud, pAssetName);
			break;
		case AXIS_SHOP_ARROWS:
			str_copy(g_Config.m_ClAssetArrow, pAssetName);
			break;
		case AXIS_SHOP_CURSORS:
			str_copy(g_Config.m_ClAssetCursor, pAssetName);
			break;
		case AXIS_SHOP_AUDIO:
			str_copy(g_Config.m_SndPack, pAssetName);
			break;
		default:
			return;
		}

		if(RefreshAssetList)
		{
			pMenus->RefreshCustomAssetsTab(gs_aAxisClientShopTypeInfos[Tab].m_AssetsTab);
		}
		else
		{
			AxisClientShopReloadAsset(pMenus, Tab, pAssetName);
		}
	}

	static bool AxisClientShopDeleteAsset(CMenus *pMenus, int Tab, const char *pAssetName)
	{
		bool AnyDeleted = false;
		bool DeleteSuccess = true;

		auto DeleteDirIfExists = [&](const char *pRelativePath) {
			char aAbsolutePath[IO_MAX_PATH_LENGTH];
			pMenus->MenuStorage()->GetCompletePath(IStorage::TYPE_SAVE, pRelativePath, aAbsolutePath, sizeof(aAbsolutePath));
			if(fs_is_dir(aAbsolutePath) == 1)
			{
				AnyDeleted = true;
				DeleteSuccess &= AxisClientShopRemoveAbsoluteDirectoryRecursive(aAbsolutePath);
			}
		};

		auto DeleteFileIfExists = [&](const char *pRelativePath) {
			if(pMenus->MenuStorage()->FileExists(pRelativePath, IStorage::TYPE_SAVE))
			{
				AnyDeleted = true;
				DeleteSuccess &= pMenus->MenuStorage()->RemoveFile(pRelativePath, IStorage::TYPE_SAVE);
			}
		};

		if(Tab == AXIS_SHOP_AUDIO)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "assets/audio/%s", pAssetName);
			DeleteDirIfExists(aPath);
			str_format(aPath, sizeof(aPath), "audio/%s", pAssetName);
			DeleteDirIfExists(aPath);
		}
		else if(Tab == AXIS_SHOP_ARROWS)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "assets/arrow/%s.png", pAssetName);
			DeleteFileIfExists(aPath);
			str_format(aPath, sizeof(aPath), "assets/arrow/%s/arrow.png", pAssetName);
			DeleteFileIfExists(aPath);
			str_format(aPath, sizeof(aPath), "assets/arrows/%s.png", pAssetName);
			DeleteFileIfExists(aPath);
			str_format(aPath, sizeof(aPath), "assets/arrows/%s/arrow.png", pAssetName);
			DeleteFileIfExists(aPath);

			str_format(aPath, sizeof(aPath), "assets/arrow/%s", pAssetName);
			DeleteDirIfExists(aPath);
			str_format(aPath, sizeof(aPath), "assets/arrows/%s", pAssetName);
			DeleteDirIfExists(aPath);
		}
		else
		{
			char aPath[IO_MAX_PATH_LENGTH];
			AxisClientShopBuildAssetPath(Tab, pAssetName, aPath, sizeof(aPath));
			if(pMenus->MenuStorage()->FileExists(aPath, IStorage::TYPE_SAVE))
			{
				AnyDeleted = true;
				DeleteSuccess = pMenus->MenuStorage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
			}
			else
			{
				char aDirectoryPath[IO_MAX_PATH_LENGTH];
				AxisClientShopBuildAssetDirectoryPath(Tab, pAssetName, aDirectoryPath, sizeof(aDirectoryPath));
				char aAbsolutePath[IO_MAX_PATH_LENGTH];
				pMenus->MenuStorage()->GetCompletePath(IStorage::TYPE_SAVE, aDirectoryPath, aAbsolutePath, sizeof(aAbsolutePath));
				if(fs_is_dir(aAbsolutePath) == 1)
				{
					AnyDeleted = true;
					DeleteSuccess = AxisClientShopRemoveAbsoluteDirectoryRecursive(aAbsolutePath);
				}
				else
				{
					return false;
				}
			}
		}

		if(!AnyDeleted)
		{
			return false;
		}

		if(AxisClientShopAssetSelected(Tab, pAssetName))
		{
			AxisClientShopApplyAsset(pMenus, Tab, "default", true);
		}
		else
		{
			pMenus->RefreshCustomAssetsTab(gs_aAxisClientShopTypeInfos[Tab].m_AssetsTab);
		}

		return DeleteSuccess;
	}

	static void AxisClientShopOpenAssetDirectory(CMenus *pMenus, int Tab)
	{
		const char *pRelativePath = gs_aAxisClientShopTypeInfos[Tab].m_pAssetDirectory;
		pMenus->MenuStorage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		pMenus->MenuStorage()->CreateFolder(pRelativePath, IStorage::TYPE_SAVE);

		char aAbsolutePath[IO_MAX_PATH_LENGTH];
		pMenus->MenuStorage()->GetCompletePath(IStorage::TYPE_SAVE, pRelativePath, aAbsolutePath, sizeof(aAbsolutePath));
		pMenus->MenuClient()->ViewFile(aAbsolutePath);
	}

	static SAxisClientShopItem *AxisClientShopFindItem(const char *pItemId)
	{
		for(SAxisClientShopItem &Item : gs_AxisClientShopState.m_vItems)
		{
			if(str_comp(Item.m_aId, pItemId) == 0)
			{
				return &Item;
			}
		}
		return nullptr;
	}

	static bool AxisClientShopLoadPreviewTexture(CMenus *pMenus, SAxisClientShopItem &Item, int Tab)
	{
		if(Tab == AXIS_SHOP_AUDIO)
		{
			return false;
		}

		if(Item.m_PreviewTexture.IsValid() || Item.m_aId[0] == '\0')
		{
			return Item.m_PreviewTexture.IsValid();
		}

		char aPreviewPath[IO_MAX_PATH_LENGTH];
		AxisClientShopBuildPreviewPath(Tab, Item.m_aId, aPreviewPath, sizeof(aPreviewPath));
		if(!pMenus->MenuStorage()->FileExists(aPreviewPath, IStorage::TYPE_SAVE))
		{
			return false;
		}

		Item.m_PreviewTexture = pMenus->MenuGraphics()->LoadTexture(aPreviewPath, IStorage::TYPE_SAVE);
		CImageInfo PreviewInfo;
		if(pMenus->MenuGraphics()->LoadPng(PreviewInfo, aPreviewPath, IStorage::TYPE_SAVE))
		{
			Item.m_PreviewWidth = PreviewInfo.m_Width;
			Item.m_PreviewHeight = PreviewInfo.m_Height;
			PreviewInfo.Free();
		}
		return Item.m_PreviewTexture.IsValid() && !Item.m_PreviewTexture.IsNullTexture();
	}

	static bool AxisClientShopCalcFittedRect(const CUIRect &Rect, int SourceWidth, int SourceHeight, CUIRect &OutRect)
	{
		if(SourceWidth <= 0 || SourceHeight <= 0 || Rect.w <= 0.0f || Rect.h <= 0.0f)
		{
			return false;
		}

		const float SourceAspect = (float)SourceWidth / (float)SourceHeight;
		const float RectAspect = Rect.w / Rect.h;

		OutRect = Rect;
		if(SourceAspect > RectAspect)
		{
			OutRect.h = Rect.w / SourceAspect;
			OutRect.y = Rect.y + (Rect.h - OutRect.h) * 0.5f;
		}
		else
		{
			OutRect.w = Rect.h * SourceAspect;
			OutRect.x = Rect.x + (Rect.w - OutRect.w) * 0.5f;
		}
		return true;
	}

	static void AxisClientShopDrawFittedTexture(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const CUIRect &Rect, int SourceWidth, int SourceHeight)
	{
		if(!Texture.IsValid() || Texture.IsNullTexture())
		{
			return;
		}

		CUIRect FittedRect;
		if(!AxisClientShopCalcFittedRect(Rect, SourceWidth, SourceHeight, FittedRect))
		{
			FittedRect = Rect;
		}

		pGraphics->WrapClamp();
		pGraphics->TextureSet(Texture);
		pGraphics->QuadsBegin();
		pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		const IGraphics::CQuadItem QuadItem(FittedRect.x, FittedRect.y, FittedRect.w, FittedRect.h);
		pGraphics->QuadsDrawTL(&QuadItem, 1);
		pGraphics->QuadsEnd();
		pGraphics->WrapNormal();
	}

	static float AxisClientShopListPreviewWidth(const SAxisClientShopItem &Item, int Tab)
	{
		float Width = Tab == AXIS_SHOP_GAME ? 112.0f : 64.0f;
		if(Item.m_PreviewWidth > 0 && Item.m_PreviewHeight > 0)
		{
			const float Aspect = (float)Item.m_PreviewWidth / (float)Item.m_PreviewHeight;
			if(Aspect > 1.0f)
			{
				Width = std::clamp((AXIS_SHOP_ITEM_HEIGHT - 12.0f) * Aspect, Width, 132.0f);
			}
		}
		return Width;
	}

	static void AxisClientShopBuildInstallUrls(int Tab, const SAxisClientShopItem &Item, std::vector<std::string> &vUrls)
	{
		vUrls.clear();

		auto AddUrl = [&vUrls](const char *pUrl) {
			if(pUrl == nullptr || pUrl[0] == '\0')
			{
				return;
			}

			char aResolvedUrl[256];
			AxisClientShopResolveUrl(pUrl, aResolvedUrl, sizeof(aResolvedUrl));
			if(aResolvedUrl[0] == '\0')
			{
				return;
			}

			const std::string Url = aResolvedUrl;
			if(std::find(vUrls.begin(), vUrls.end(), Url) == vUrls.end())
			{
				vUrls.push_back(Url);
			}
		};

		char aFallbackUrl[256];
		if(Tab == AXIS_SHOP_AUDIO)
		{
			AddUrl(Item.m_aDownloadUrl);

			str_format(aFallbackUrl, sizeof(aFallbackUrl), "%s/api/skins/%s?download=true", AXIS_SHOP_HOST, Item.m_aId);
			AddUrl(aFallbackUrl);

			str_format(aFallbackUrl, sizeof(aFallbackUrl), "%s/api/skins/%s?file=true", AXIS_SHOP_HOST, Item.m_aId);
			AddUrl(aFallbackUrl);
		}

		AddUrl(Item.m_aImageUrl);

		str_format(aFallbackUrl, sizeof(aFallbackUrl), "%s/api/skins/%s?image=true", AXIS_SHOP_HOST, Item.m_aId);
		AddUrl(aFallbackUrl);
	}

	static void AxisClientShopStartFetch(CMenus *pMenus)
	{
		const char *pApiType = gs_aAxisClientShopTypeInfos[gs_AxisClientShopState.m_Tab].m_pApiType;
		char aUrl[512];
		if(AxisClientShopHasActiveSearch())
		{
			char aEscapedQuery[384];
			EscapeUrl(aEscapedQuery, sizeof(aEscapedQuery), gs_AxisClientShopState.m_aAppliedSearch);
			str_format(aUrl, sizeof(aUrl), AXIS_SHOP_SEARCH_API_URL, 1, pApiType, aEscapedQuery);
		}
		else
		{
			str_format(aUrl, sizeof(aUrl), AXIS_SHOP_BROWSE_API_URL, gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab], pApiType);
		}

		gs_AxisClientShopState.m_pFetchTask = HttpGet(aUrl);
		gs_AxisClientShopState.m_pFetchTask->Timeout(AXIS_SHOP_TIMEOUT);
		gs_AxisClientShopState.m_pFetchTask->IpResolve(IPRESOLVE::V4);
		gs_AxisClientShopState.m_pFetchTask->MaxResponseSize(AXIS_SHOP_PAGE_MAX_RESPONSE_SIZE);
		gs_AxisClientShopState.m_pFetchTask->LogProgress(HTTPLOG::NONE);
		gs_AxisClientShopState.m_pFetchTask->FailOnErrorStatus(false);
		gs_AxisClientShopState.m_FetchTab = gs_AxisClientShopState.m_Tab;
		gs_AxisClientShopState.m_FetchPage = AxisClientShopHasActiveSearch() ? 1 : gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab];
		str_copy(gs_AxisClientShopState.m_aFetchSearch, gs_AxisClientShopState.m_aAppliedSearch, sizeof(gs_AxisClientShopState.m_aFetchSearch));
		AxisClientShopSetStatus(Localize("Loading shop..."));
		pMenus->MenuHttp()->Run(gs_AxisClientShopState.m_pFetchTask);
	}

	static void AxisClientShopEnsureFetch(CMenus *pMenus)
	{
		if(gs_AxisClientShopState.m_pFetchTask)
		{
			return;
		}

		const int CurrentPage = AxisClientShopHasActiveSearch() ? 1 : gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab];
		if(gs_AxisClientShopState.m_LoadedTab != gs_AxisClientShopState.m_Tab ||
			gs_AxisClientShopState.m_LoadedPage != CurrentPage ||
			str_comp(gs_AxisClientShopState.m_aLoadedSearch, gs_AxisClientShopState.m_aAppliedSearch) != 0)
		{
			AxisClientShopStartFetch(pMenus);
		}
	}

	static void AxisClientShopStartPreviewFetch(CMenus *pMenus)
	{
		if(gs_AxisClientShopState.m_Tab == AXIS_SHOP_AUDIO)
		{
			return;
		}

		if(gs_AxisClientShopState.m_pPreviewTask != nullptr)
		{
			return;
		}

		for(SAxisClientShopItem &Item : gs_AxisClientShopState.m_vItems)
		{
			if(Item.m_PreviewFailed)
			{
				continue;
			}
			if(AxisClientShopLoadPreviewTexture(pMenus, Item, gs_AxisClientShopState.m_Tab))
			{
				continue;
			}

			char aPreviewUrl[256];
			if(Item.m_aImageUrl[0] != '\0')
			{
				AxisClientShopResolveUrl(Item.m_aImageUrl, aPreviewUrl, sizeof(aPreviewUrl));
			}
			else
			{
				str_format(aPreviewUrl, sizeof(aPreviewUrl), "%s/api/skins/%s?image=true", AXIS_SHOP_HOST, Item.m_aId);
			}

			if(aPreviewUrl[0] == '\0')
			{
				Item.m_PreviewFailed = true;
				continue;
			}

			AxisClientShopBuildPreviewPath(gs_AxisClientShopState.m_Tab, Item.m_aId, gs_AxisClientShopState.m_aPreviewPath, sizeof(gs_AxisClientShopState.m_aPreviewPath));
			str_copy(gs_AxisClientShopState.m_aPreviewItemId, Item.m_aId, sizeof(gs_AxisClientShopState.m_aPreviewItemId));
			gs_AxisClientShopState.m_PreviewTab = gs_AxisClientShopState.m_Tab;
			gs_AxisClientShopState.m_PreviewPage = AxisClientShopHasActiveSearch() ? 1 : gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab];
			str_copy(gs_AxisClientShopState.m_aPreviewSearch, gs_AxisClientShopState.m_aAppliedSearch, sizeof(gs_AxisClientShopState.m_aPreviewSearch));

			gs_AxisClientShopState.m_pPreviewTask = HttpGet(aPreviewUrl);
			gs_AxisClientShopState.m_pPreviewTask->Timeout(AXIS_SHOP_TIMEOUT);
			gs_AxisClientShopState.m_pPreviewTask->IpResolve(IPRESOLVE::V4);
			gs_AxisClientShopState.m_pPreviewTask->MaxResponseSize(AXIS_SHOP_IMAGE_MAX_RESPONSE_SIZE);
			gs_AxisClientShopState.m_pPreviewTask->LogProgress(HTTPLOG::NONE);
			gs_AxisClientShopState.m_pPreviewTask->FailOnErrorStatus(false);
			pMenus->MenuHttp()->Run(gs_AxisClientShopState.m_pPreviewTask);
			return;
		}
	}

	static void AxisClientShopFinishPreviewFetch(CMenus *pMenus)
	{
		if(!gs_AxisClientShopState.m_pPreviewTask || !gs_AxisClientShopState.m_pPreviewTask->Done())
		{
			return;
		}

		SAxisClientShopItem *pItem = AxisClientShopFindItem(gs_AxisClientShopState.m_aPreviewItemId);
		const bool SamePage = gs_AxisClientShopState.m_PreviewTab == gs_AxisClientShopState.m_Tab &&
				      gs_AxisClientShopState.m_PreviewPage == (AxisClientShopHasActiveSearch() ? 1 : gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab]) &&
				      str_comp(gs_AxisClientShopState.m_aPreviewSearch, gs_AxisClientShopState.m_aAppliedSearch) == 0;

		if(gs_AxisClientShopState.m_pPreviewTask->State() != EHttpState::DONE)
		{
			if(pItem != nullptr)
			{
				pItem->m_PreviewFailed = true;
			}
			AxisClientShopAbortPreviewTask();
			return;
		}

		if(pItem != nullptr)
		{
			if(!SamePage || gs_AxisClientShopState.m_pPreviewTask->StatusCode() >= 400)
			{
				pItem->m_PreviewFailed = true;
			}
			else
			{
				unsigned char *pResult = nullptr;
				size_t ResultLength = 0;
				gs_AxisClientShopState.m_pPreviewTask->Result(&pResult, &ResultLength);
				if(pResult != nullptr && ResultLength > 0 && AxisClientShopIsPngBuffer(pResult, ResultLength) &&
					AxisClientShopWriteFile(pMenus, gs_AxisClientShopState.m_aPreviewPath, pResult, ResultLength) &&
					AxisClientShopLoadPreviewTexture(pMenus, *pItem, gs_AxisClientShopState.m_Tab))
				{
					pItem->m_PreviewFailed = false;
				}
				else
				{
					pItem->m_PreviewFailed = true;
				}
			}
		}

		AxisClientShopAbortPreviewTask();
	}

	static void AxisClientShopFinishFetch(CMenus *pMenus)
	{
		if(!gs_AxisClientShopState.m_pFetchTask || !gs_AxisClientShopState.m_pFetchTask->Done())
		{
			return;
		}

		gs_AxisClientShopState.m_LoadedTab = gs_AxisClientShopState.m_FetchTab;
		gs_AxisClientShopState.m_LoadedPage = gs_AxisClientShopState.m_FetchPage;
		str_copy(gs_AxisClientShopState.m_aLoadedSearch, gs_AxisClientShopState.m_aFetchSearch, sizeof(gs_AxisClientShopState.m_aLoadedSearch));

		if(gs_AxisClientShopState.m_pFetchTask->State() != EHttpState::DONE)
		{
			AxisClientShopClearItems(pMenus);
			gs_AxisClientShopState.m_TotalPages = 1;
			gs_AxisClientShopState.m_TotalItems = 0;
			AxisClientShopSetStatus(Localize("Shop request failed"));
			return;
		}

		const int FetchStatusCode = gs_AxisClientShopState.m_pFetchTask->StatusCode();
		if(FetchStatusCode >= 400)
		{
			str_format(gs_AxisClientShopState.m_aStatus, sizeof(gs_AxisClientShopState.m_aStatus), "%s (%d)", Localize("Shop request failed"), FetchStatusCode);
			return;
		}

		json_value *pJson = gs_AxisClientShopState.m_pFetchTask->ResultJson();
		if(pJson == nullptr || pJson->type != json_object)
		{
			if(pJson != nullptr)
			{
				json_value_free(pJson);
			}
			AxisClientShopSetStatus(Localize("Shop response is invalid"));
			return;
		}

		std::vector<SAxisClientShopItem> vItems;
		const json_value *pSkins = json_object_get(pJson, "skins");
		if(pSkins != &json_value_none && pSkins->type == json_array)
		{
			for(int Index = 0; Index < json_array_length(pSkins); ++Index)
			{
				const json_value *pSkin = json_array_get(pSkins, Index);
				if(pSkin == &json_value_none || pSkin->type != json_object)
				{
					continue;
				}

				const char *pStatus = json_string_get(json_object_get(pSkin, "status"));
				if(pStatus != nullptr && pStatus[0] != '\0' && str_comp(pStatus, "approved") != 0)
				{
					continue;
				}

				SAxisClientShopItem Item;
				if(const char *pId = json_string_get(json_object_get(pSkin, "id")); pId != nullptr)
				{
					str_copy(Item.m_aId, pId, sizeof(Item.m_aId));
				}
				if(const char *pName = json_string_get(json_object_get(pSkin, "name")); pName != nullptr)
				{
					str_copy(Item.m_aName, pName, sizeof(Item.m_aName));
				}
				if(const char *pFilename = json_string_get(json_object_get(pSkin, "filename")); pFilename != nullptr)
				{
					str_copy(Item.m_aFilename, pFilename, sizeof(Item.m_aFilename));
				}
				if(const char *pUsername = json_string_get(json_object_get(pSkin, "username")); pUsername != nullptr)
				{
					str_copy(Item.m_aUsername, pUsername, sizeof(Item.m_aUsername));
				}
				if(const char *pImageUrl = json_string_get(json_object_get(pSkin, "imageUrl")); pImageUrl != nullptr)
				{
					str_copy(Item.m_aImageUrl, pImageUrl, sizeof(Item.m_aImageUrl));
				}
				if(const char *pDownloadUrl = json_string_get(json_object_get(pSkin, "downloadUrl")); pDownloadUrl != nullptr)
				{
					str_copy(Item.m_aDownloadUrl, pDownloadUrl, sizeof(Item.m_aDownloadUrl));
				}
				else if(const char *pFileUrl = json_string_get(json_object_get(pSkin, "fileUrl")); pFileUrl != nullptr)
				{
					str_copy(Item.m_aDownloadUrl, pFileUrl, sizeof(Item.m_aDownloadUrl));
				}
				if(Item.m_aId[0] == '\0')
				{
					continue;
				}
				if(Item.m_aName[0] == '\0')
				{
					str_copy(Item.m_aName, Item.m_aFilename, sizeof(Item.m_aName));
				}

				vItems.push_back(Item);
			}
		}

		int TotalPages = 1;
		const json_value *pTotalPages = json_object_get(pJson, "totalPages");
		if(!AxisClientShopHasActiveSearch() && pTotalPages != &json_value_none && pTotalPages->type == json_integer)
		{
			TotalPages = maximum(1, json_int_get(pTotalPages));
		}

		int TotalItems = (int)vItems.size();
		const json_value *pTotal = json_object_get(pJson, "total");
		if(pTotal != &json_value_none && pTotal->type == json_integer)
		{
			TotalItems = maximum(0, json_int_get(pTotal));
		}

		AxisClientShopClearItems(pMenus);
		gs_AxisClientShopState.m_vItems = std::move(vItems);
		gs_AxisClientShopState.m_SelectedIndex = gs_AxisClientShopState.m_vItems.empty() ? -1 : 0;
		gs_AxisClientShopState.m_TotalPages = TotalPages;
		gs_AxisClientShopState.m_TotalItems = TotalItems;

		if(!AxisClientShopHasActiveSearch() && gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab] > gs_AxisClientShopState.m_TotalPages)
		{
			gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab] = gs_AxisClientShopState.m_TotalPages;
		}

		if(gs_AxisClientShopState.m_vItems.empty())
		{
			AxisClientShopSetStatus(Localize("No assets found on this page"));
		}
		else
		{
			gs_AxisClientShopState.m_aStatus[0] = '\0';
		}

		json_value_free(pJson);
	}

	static bool AxisClientShopInstallDownloadedData(CMenus *pMenus, const unsigned char *pData, size_t DataSize)
	{
		if(gs_AxisClientShopState.m_InstallTab < 0 || gs_AxisClientShopState.m_InstallTab >= NUM_AXIS_SHOP_TABS)
		{
			return false;
		}

		const int InstallTab = gs_AxisClientShopState.m_InstallTab;
		if(AxisClientShopIsPngBuffer(pData, DataSize))
		{
			if(InstallTab == AXIS_SHOP_AUDIO)
			{
				return false;
			}

			char aAssetPath[IO_MAX_PATH_LENGTH];
			AxisClientShopBuildAssetPath(InstallTab, gs_AxisClientShopState.m_aInstallAssetName, aAssetPath, sizeof(aAssetPath));
			return AxisClientShopWriteFile(pMenus, aAssetPath, pData, DataSize);
		}

		if(AxisClientShopIsZipBuffer(pData, DataSize))
		{
			char aAssetDirectoryPath[IO_MAX_PATH_LENGTH];
			AxisClientShopBuildAssetDirectoryPath(InstallTab, gs_AxisClientShopState.m_aInstallAssetName, aAssetDirectoryPath, sizeof(aAssetDirectoryPath));
			char aAbsolutePath[IO_MAX_PATH_LENGTH];
			pMenus->MenuStorage()->GetCompletePath(IStorage::TYPE_SAVE, aAssetDirectoryPath, aAbsolutePath, sizeof(aAbsolutePath));
			if(fs_is_dir(aAbsolutePath) == 1 && !AxisClientShopRemoveAbsoluteDirectoryRecursive(aAbsolutePath))
			{
				return false;
			}
			return AxisClientShopExtractZipToDirectory(pMenus, pData, DataSize, aAssetDirectoryPath);
		}

		return false;
	}

	static void AxisClientShopStartInstallRequest(CMenus *pMenus)
	{
		if(gs_AxisClientShopState.m_InstallUrlIndex < 0 || gs_AxisClientShopState.m_InstallUrlIndex >= (int)gs_AxisClientShopState.m_vInstallUrls.size())
		{
			AxisClientShopSetStatus(Localize("Failed to build a download URL"));
			return;
		}

		const std::string &Url = gs_AxisClientShopState.m_vInstallUrls[gs_AxisClientShopState.m_InstallUrlIndex];
		gs_AxisClientShopState.m_pInstallTask = HttpGet(Url.c_str());
		gs_AxisClientShopState.m_pInstallTask->Timeout(AXIS_SHOP_TIMEOUT);
		gs_AxisClientShopState.m_pInstallTask->IpResolve(IPRESOLVE::V4);
		const int64_t MaxResponseSize = gs_AxisClientShopState.m_InstallTab == AXIS_SHOP_AUDIO ?
							AXIS_SHOP_AUDIO_MAX_RESPONSE_SIZE :
							AXIS_SHOP_IMAGE_MAX_RESPONSE_SIZE;
		gs_AxisClientShopState.m_pInstallTask->MaxResponseSize(MaxResponseSize);
		gs_AxisClientShopState.m_pInstallTask->LogProgress(HTTPLOG::NONE);
		gs_AxisClientShopState.m_pInstallTask->FailOnErrorStatus(false);

		char aMessage[256];
		str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Downloading"), gs_AxisClientShopState.m_aInstallAssetName);
		AxisClientShopSetStatus(aMessage);
		pMenus->MenuHttp()->Run(gs_AxisClientShopState.m_pInstallTask);
	}

	static void AxisClientShopStartInstall(CMenus *pMenus, int Tab, const SAxisClientShopItem &Item)
	{
		AxisClientShopAbortTask(gs_AxisClientShopState.m_pInstallTask);
		AxisClientShopResetInstallState();
		gs_AxisClientShopState.m_InstallTab = Tab;
		str_copy(gs_AxisClientShopState.m_aInstallItemId, Item.m_aId, sizeof(gs_AxisClientShopState.m_aInstallItemId));
		AxisClientShopNormalizeAssetName(Item.m_aName, Item.m_aFilename, gs_AxisClientShopState.m_aInstallAssetName, sizeof(gs_AxisClientShopState.m_aInstallAssetName));
		AxisClientShopBuildInstallUrls(Tab, Item, gs_AxisClientShopState.m_vInstallUrls);
		AxisClientShopStartInstallRequest(pMenus);
	}

	static void AxisClientShopRetryInstall(CMenus *pMenus)
	{
		AxisClientShopAbortTask(gs_AxisClientShopState.m_pInstallTask);
		++gs_AxisClientShopState.m_InstallUrlIndex;
		if(gs_AxisClientShopState.m_InstallUrlIndex >= (int)gs_AxisClientShopState.m_vInstallUrls.size())
		{
			char aMessage[256];
			str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Unable to install asset"), gs_AxisClientShopState.m_aInstallAssetName);
			AxisClientShopSetStatus(aMessage);
			AxisClientShopResetInstallState();
			return;
		}

		AxisClientShopStartInstallRequest(pMenus);
	}

	static void AxisClientShopFinishInstall(CMenus *pMenus)
	{
		if(!gs_AxisClientShopState.m_pInstallTask || !gs_AxisClientShopState.m_pInstallTask->Done())
		{
			return;
		}

		if(gs_AxisClientShopState.m_pInstallTask->State() != EHttpState::DONE)
		{
			AxisClientShopRetryInstall(pMenus);
			return;
		}

		if(gs_AxisClientShopState.m_pInstallTask->StatusCode() >= 400)
		{
			AxisClientShopRetryInstall(pMenus);
			return;
		}

		unsigned char *pResult = nullptr;
		size_t ResultLength = 0;
		gs_AxisClientShopState.m_pInstallTask->Result(&pResult, &ResultLength);
		if(pResult == nullptr || ResultLength == 0 || !AxisClientShopInstallDownloadedData(pMenus, pResult, ResultLength))
		{
			AxisClientShopRetryInstall(pMenus);
			return;
		}

		char aMessage[256];
		if(g_Config.m_ClAClientShopAutoSet)
		{
			AxisClientShopApplyAsset(pMenus, gs_AxisClientShopState.m_InstallTab, gs_AxisClientShopState.m_aInstallAssetName, true);
			str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Installed and applied"), gs_AxisClientShopState.m_aInstallAssetName);
		}
		else
		{
			pMenus->RefreshCustomAssetsTab(gs_aAxisClientShopTypeInfos[gs_AxisClientShopState.m_InstallTab].m_AssetsTab);
			str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Installed"), gs_AxisClientShopState.m_aInstallAssetName);
		}
		AxisClientShopSetStatus(aMessage);

		AxisClientShopAbortTask(gs_AxisClientShopState.m_pInstallTask);
		AxisClientShopResetInstallState();
	}

	static void AxisClientShopRenderPreview(CMenus *pMenus, const CUIRect &MainView)
	{
		SAxisClientShopItem *pItem = AxisClientShopFindItem(gs_AxisClientShopState.m_aOpenPreviewItemId);
		if(pItem == nullptr || !pItem->m_PreviewTexture.IsValid() || pItem->m_PreviewTexture.IsNullTexture())
		{
			AxisClientShopClosePreview();
			return;
		}

		CUIRect Overlay = MainView;
		Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f), IGraphics::CORNER_ALL, 0.0f);

		CUIRect Panel = Overlay;
		Panel.VMargin(maximum(32.0f, (Overlay.w - 860.0f) / 2.0f), &Panel);
		Panel.HMargin(maximum(24.0f, (Overlay.h - 600.0f) / 2.0f), &Panel);
		Panel.Draw(ColorRGBA(0.06f, 0.07f, 0.08f, 0.96f), IGraphics::CORNER_ALL, AXIS_SHOP_SECTION_ROUNDING + 2.0f);

		CUIRect Content;
		Panel.Margin(AXIS_SHOP_MARGIN + 4.0f, &Content);

		CUIRect Header, PreviewArea;
		Content.HSplitTop(28.0f, &Header, &Content);
		Content.HSplitTop(AXIS_SHOP_MARGIN_SMALL, nullptr, &Content);
		PreviewArea = Content;

		CUIRect Title, CloseButton;
		Header.VSplitRight(100.0f, &Title, &CloseButton);
		pMenus->MenuUi()->DoLabel(&Title, pItem->m_aName, AXIS_SHOP_HEADLINE_FONT_SIZE, TEXTALIGN_ML);
		if(pMenus->DoButton_Menu(&gs_AxisClientShopState.m_PreviewCloseButton, Localize("Close"), 0, &CloseButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f)))
		{
			AxisClientShopClosePreview();
			return;
		}

		PreviewArea.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), IGraphics::CORNER_ALL, AXIS_SHOP_SECTION_ROUNDING);
		PreviewArea.Margin(AXIS_SHOP_MARGIN, &PreviewArea);
		PreviewArea.Draw(ColorRGBA(0.12f, 0.12f, 0.12f, 1.0f), IGraphics::CORNER_ALL, AXIS_SHOP_SECTION_ROUNDING - 2.0f);

		CUIRect TextureRect;
		PreviewArea.Margin(AXIS_SHOP_MARGIN, &TextureRect);
		AxisClientShopDrawFittedTexture(pMenus->MenuGraphics(), pItem->m_PreviewTexture, TextureRect, pItem->m_PreviewWidth, pItem->m_PreviewHeight);
	}

} // namespace

void CMenus::RenderSettingsAxisClientShop(CUIRect MainView)
{
	const CUIRect FullView = MainView;
	AxisClientShopInitState();

	if(gs_AxisClientShopState.m_pFetchTask && gs_AxisClientShopState.m_pFetchTask->Done())
	{
		AxisClientShopFinishFetch(this);
		gs_AxisClientShopState.m_pFetchTask = nullptr;
		gs_AxisClientShopState.m_FetchTab = -1;
		gs_AxisClientShopState.m_FetchPage = 0;
		gs_AxisClientShopState.m_aFetchSearch[0] = '\0';
	}

	if(gs_AxisClientShopState.m_pPreviewTask && gs_AxisClientShopState.m_pPreviewTask->Done())
	{
		AxisClientShopFinishPreviewFetch(this);
	}

	if(gs_AxisClientShopState.m_pInstallTask && gs_AxisClientShopState.m_pInstallTask->Done())
	{
		AxisClientShopFinishInstall(this);
	}

	if(AxisClientShopHasPreviewOpen())
	{
		AxisClientShopRenderPreview(this, FullView);
		return;
	}

	CUIRect ControlsRow, StatusRow, ListView, TabsArea, TabsRow;
	MainView.HSplitTop(AXIS_SHOP_LINE_SIZE, &ControlsRow, &MainView);
	MainView.HSplitTop(AXIS_SHOP_MARGIN_SMALL, nullptr, &MainView);
	MainView.HSplitTop(AXIS_SHOP_LINE_SIZE, &StatusRow, &MainView);
	MainView.HSplitTop(AXIS_SHOP_MARGIN_SMALL, nullptr, &MainView);
	MainView.HSplitBottom(AXIS_SHOP_TAB_HEIGHT + AXIS_SHOP_MARGIN_SMALL, &ListView, &TabsArea);
	TabsArea.HSplitTop(AXIS_SHOP_MARGIN_SMALL, nullptr, &TabsRow);

	CUIRect SearchBox, SearchButton, RefreshButton, FolderButton, AutoSetButton, PrevButton, PageLabel, NextButton;
	CUIRect ControlsRest = ControlsRow;
	CUIRect Spacer;
	const bool SearchMode = AxisClientShopHasActiveSearch();
	ControlsRest.VSplitRight(90.0f, &ControlsRest, &AutoSetButton);
	ControlsRest.VSplitRight(AXIS_SHOP_MARGIN_SMALL, &ControlsRest, &Spacer);
	ControlsRest.VSplitRight(120.0f, &ControlsRest, &FolderButton);
	ControlsRest.VSplitRight(AXIS_SHOP_MARGIN_SMALL, &ControlsRest, &Spacer);
	ControlsRest.VSplitRight(AXIS_SHOP_LINE_SIZE, &ControlsRest, &RefreshButton);
	ControlsRest.VSplitRight(AXIS_SHOP_MARGIN_SMALL, &ControlsRest, &Spacer);
	ControlsRest.VSplitRight(72.0f, &ControlsRest, &SearchButton);
	ControlsRest.VSplitRight(AXIS_SHOP_MARGIN_SMALL, &ControlsRest, &Spacer);

	if(SearchMode)
	{
		SearchBox = ControlsRest;
	}
	else
	{
		ControlsRest.VSplitLeft(24.0f, &PrevButton, &ControlsRest);
		ControlsRest.VSplitLeft(2.0f, &Spacer, &ControlsRest);
		ControlsRest.VSplitLeft(56.0f, &PageLabel, &ControlsRest);
		ControlsRest.VSplitLeft(2.0f, &Spacer, &ControlsRest);
		ControlsRest.VSplitLeft(24.0f, &NextButton, &ControlsRest);
		ControlsRest.VSplitLeft(AXIS_SHOP_MARGIN_SMALL, &Spacer, &ControlsRest);
		SearchBox = ControlsRest;
	}

	Ui()->DoEditBox_Search(&gs_AxisClientShopSearchInput, &SearchBox, 12.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	static CButtonContainer s_SearchButton;
	static CButtonContainer s_RefreshButton;
	static CButtonContainer s_OpenFolderButton;
	static CButtonContainer s_AutoSetButton;
	static CButtonContainer s_PrevButton;
	static CButtonContainer s_NextButton;

	const bool SearchHotkey = gs_AxisClientShopSearchInput.IsActive() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER);
	if(DoButton_Menu(&s_SearchButton, Localize("Search"), 0, &SearchButton) || SearchHotkey)
	{
		AxisClientShopSetSearch(this, gs_AxisClientShopSearchInput.GetString());
	}

	if(Ui()->DoButton_FontIcon(&s_RefreshButton, ARROW_ROTATE_RIGHT, 0, &RefreshButton, IGraphics::CORNER_ALL))
	{
		AxisClientShopInvalidatePage(this);
	}

	if(DoButton_Menu(&s_OpenFolderButton, Localize("Assets directory"), 0, &FolderButton))
	{
		AxisClientShopOpenAssetDirectory(this, gs_AxisClientShopState.m_Tab);
	}

	if(DoButton_CheckBox(&s_AutoSetButton, Localize("Auto set"), g_Config.m_ClAClientShopAutoSet, &AutoSetButton))
	{
		g_Config.m_ClAClientShopAutoSet = !g_Config.m_ClAClientShopAutoSet;
	}

	if(!SearchMode)
	{
		const int CurrentPage = gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab];
		if(DoButton_Menu(&s_PrevButton, "<", CurrentPage > 1 ? 0 : -1, &PrevButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, CurrentPage > 1 ? 0.25f : 0.15f)) && CurrentPage > 1)
		{
			AxisClientShopSetPage(this, CurrentPage - 1);
		}

		char aPageLabel[128];
		str_format(aPageLabel, sizeof(aPageLabel), "%d/%d", CurrentPage, maximum(1, gs_AxisClientShopState.m_TotalPages));
		Ui()->DoLabel(&PageLabel, aPageLabel, AXIS_SHOP_FONT_SIZE, TEXTALIGN_MC);

		if(DoButton_Menu(&s_NextButton, ">", CurrentPage < gs_AxisClientShopState.m_TotalPages ? 0 : -1, &NextButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, CurrentPage < gs_AxisClientShopState.m_TotalPages ? 0.25f : 0.15f)) && CurrentPage < gs_AxisClientShopState.m_TotalPages)
		{
			AxisClientShopSetPage(this, CurrentPage + 1);
		}
	}

	if(gs_AxisClientShopState.m_LoadedTab != gs_AxisClientShopState.m_Tab ||
		gs_AxisClientShopState.m_LoadedPage != (SearchMode ? 1 : gs_AxisClientShopState.m_aPages[gs_AxisClientShopState.m_Tab]) ||
		str_comp(gs_AxisClientShopState.m_aLoadedSearch, gs_AxisClientShopState.m_aAppliedSearch) != 0)
	{
		AxisClientShopEnsureFetch(this);
	}
	else
	{
		AxisClientShopStartPreviewFetch(this);
	}

	char aStatusText[256];
	if(gs_AxisClientShopState.m_aStatus[0] != '\0')
	{
		str_copy(aStatusText, gs_AxisClientShopState.m_aStatus, sizeof(aStatusText));
	}
	else if(SearchMode)
	{
		str_format(aStatusText, sizeof(aStatusText), "%s: %d", Localize("Search results"), gs_AxisClientShopState.m_TotalItems);
	}
	else
	{
		str_format(aStatusText, sizeof(aStatusText), "%s: %d", Localize("Items"), gs_AxisClientShopState.m_TotalItems);
	}
	static constexpr const char *s_pCreditPrefix = "\xc2\xa9 powered by ";
	static constexpr const char *s_pCreditLink = "CatData";
	const float CreditPrefixWidth = TextRender()->TextWidth(AXIS_SHOP_SMALL_FONT_SIZE, s_pCreditPrefix, -1, -1.0f);
	const float CreditLinkWidth = TextRender()->TextWidth(AXIS_SHOP_SMALL_FONT_SIZE, s_pCreditLink, -1, -1.0f);
	const float CreditWidth = CreditPrefixWidth + CreditLinkWidth + 6.0f;
	CUIRect StatusTextLabel, StatusActions, CreditLabel, CancelButton;
	const float StatusActionsWidth = CreditWidth + (gs_AxisClientShopState.m_pInstallTask != nullptr ? AXIS_SHOP_MARGIN_SMALL + 72.0f : 0.0f);
	StatusRow.VSplitRight(StatusActionsWidth, &StatusTextLabel, &StatusActions);
	StatusActions.VSplitRight(CreditWidth, &StatusActions, &CreditLabel);
	if(gs_AxisClientShopState.m_pInstallTask != nullptr)
	{
		StatusActions.VSplitRight(AXIS_SHOP_MARGIN_SMALL, &StatusActions, nullptr);
		StatusActions.VSplitRight(72.0f, &StatusActions, &CancelButton);
	}
	Ui()->DoLabel(&StatusTextLabel, aStatusText, AXIS_SHOP_SMALL_FONT_SIZE, TEXTALIGN_ML);
	CUIRect CreditPrefixLabel, CreditLinkLabel;
	CreditLabel.VSplitRight(CreditLinkWidth, &CreditPrefixLabel, &CreditLinkLabel);
	Ui()->DoLabel(&CreditPrefixLabel, s_pCreditPrefix, AXIS_SHOP_SMALL_FONT_SIZE, TEXTALIGN_MR);
	static CButtonContainer s_CreditLinkButton;
	if(Ui()->DoButtonLogic(&s_CreditLinkButton, 0, &CreditLinkLabel, BUTTONFLAG_LEFT))
	{
		MenuClient()->ViewLink(AXIS_SHOP_HOST);
	}
	const bool CreditHot = Ui()->HotItem() == &s_CreditLinkButton;
	Ui()->DoLabel(&CreditLinkLabel, s_pCreditLink, AXIS_SHOP_SMALL_FONT_SIZE, TEXTALIGN_MR);
	if(CreditHot)
	{
		CUIRect Underline = CreditLinkLabel;
		Underline.HSplitTop(Underline.h - 2.0f, nullptr, &Underline);
		Underline.Draw(ColorRGBA(0.60f, 0.85f, 1.0f, 0.8f), IGraphics::CORNER_NONE, 0.0f);
	}
	static CButtonContainer s_CancelInstallButton;
	if(gs_AxisClientShopState.m_pInstallTask != nullptr && DoButton_Menu(&s_CancelInstallButton, Localize("Cancel"), 0, &CancelButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.25f, 0.10f, 0.10f, 0.35f)))
	{
		AxisClientShopCancelInstall();
	}

	static CListBox s_ListBox;
	const int NumItems = (int)gs_AxisClientShopState.m_vItems.size();
	s_ListBox.DoStart(AXIS_SHOP_ITEM_HEIGHT, NumItems, 1, 1, gs_AxisClientShopState.m_SelectedIndex, &ListView, true);

	for(int Index = 0; Index < NumItems; ++Index)
	{
		SAxisClientShopItem &Item = gs_AxisClientShopState.m_vItems[Index];
		const CListboxItem ListItem = s_ListBox.DoNextItem(&Item, Index == gs_AxisClientShopState.m_SelectedIndex);
		if(!ListItem.m_Visible)
		{
			continue;
		}

		CUIRect Row = ListItem.m_Rect;
		Row.Margin(6.0f, &Row);

		CUIRect PreviewButtonRect, ContentRect;
		Row.VSplitLeft(AxisClientShopListPreviewWidth(Item, gs_AxisClientShopState.m_Tab), &PreviewButtonRect, &ContentRect);
		ContentRect.VSplitLeft(AXIS_SHOP_MARGIN, nullptr, &ContentRect);
		if(!Item.m_PreviewTexture.IsValid())
		{
			AxisClientShopLoadPreviewTexture(this, Item, gs_AxisClientShopState.m_Tab);
		}

		const bool CanOpenPreview = Item.m_PreviewTexture.IsValid() && !Item.m_PreviewTexture.IsNullTexture();
		if(Ui()->DoButtonLogic(&Item.m_PreviewButton, 0, &PreviewButtonRect, BUTTONFLAG_LEFT))
		{
			if(CanOpenPreview)
			{
				gs_AxisClientShopState.m_PreviewOpen = true;
				str_copy(gs_AxisClientShopState.m_aOpenPreviewItemId, Item.m_aId, sizeof(gs_AxisClientShopState.m_aOpenPreviewItemId));
			}
			else
			{
				AxisClientShopSetStatus(Item.m_PreviewFailed ? Localize("Preview unavailable") : Localize("Preview is still loading"));
			}
		}

		PreviewButtonRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_ALL, 8.0f);
		CUIRect PreviewRect;
		PreviewButtonRect.Margin(5.0f, &PreviewRect);
		if(CanOpenPreview)
		{
			AxisClientShopDrawFittedTexture(Graphics(), Item.m_PreviewTexture, PreviewRect, Item.m_PreviewWidth, Item.m_PreviewHeight);
		}
		else
		{
			RenderFontIcon(PreviewButtonRect, IMAGE, 22.0f, TEXTALIGN_MC);
		}

		char aAssetName[128];
		AxisClientShopNormalizeAssetName(Item.m_aName, Item.m_aFilename, aAssetName, sizeof(aAssetName));

		const bool Installed = AxisClientShopAssetExists(this, gs_AxisClientShopState.m_Tab, aAssetName);
		const bool Selected = AxisClientShopAssetSelected(gs_AxisClientShopState.m_Tab, aAssetName);
		const bool InstallingThisItem = gs_AxisClientShopState.m_pInstallTask != nullptr && str_comp(gs_AxisClientShopState.m_aInstallItemId, Item.m_aId) == 0;

		const char *pActionLabel = Localize("Download");
		int ActionState = 0;
		if(InstallingThisItem)
		{
			pActionLabel = Localize("Cancel");
		}
		else if(Selected)
		{
			pActionLabel = Localize("Applied");
			ActionState = 1;
		}
		else if(Installed)
		{
			pActionLabel = Localize("Apply");
		}

		CUIRect InfoRect, ButtonsRect;
		ContentRect.VSplitRight(98.0f, &InfoRect, &ButtonsRect);

		CUIRect NameLabel, AuthorLabel, FileLabel;
		InfoRect.HSplitTop(20.0f, &NameLabel, &InfoRect);
		InfoRect.HSplitTop(16.0f, &AuthorLabel, &InfoRect);
		InfoRect.HSplitTop(16.0f, &FileLabel, &InfoRect);

		Ui()->DoLabel(&NameLabel, Item.m_aName, 15.0f, TEXTALIGN_ML);

		char aAuthorLabel[160];
		if(Item.m_aUsername[0] != '\0')
		{
			str_format(aAuthorLabel, sizeof(aAuthorLabel), "%s: %s", Localize("Author"), Item.m_aUsername);
		}
		else
		{
			str_copy(aAuthorLabel, Localize("Unknown author"), sizeof(aAuthorLabel));
		}
		Ui()->DoLabel(&AuthorLabel, aAuthorLabel, AXIS_SHOP_SMALL_FONT_SIZE, TEXTALIGN_ML);

		char aFileLabel[160];
		str_format(aFileLabel, sizeof(aFileLabel), "%s: %s", Localize("Saved as"), aAssetName);
		Ui()->DoLabel(&FileLabel, aFileLabel, AXIS_SHOP_SMALL_FONT_SIZE, TEXTALIGN_ML);

		CUIRect ActionButton, DeleteButton;
		ButtonsRect.HSplitTop(22.0f, &ActionButton, &ButtonsRect);
		ButtonsRect.HSplitTop(AXIS_SHOP_MARGIN_SMALL, nullptr, &ButtonsRect);
		DeleteButton = ButtonsRect;
		DeleteButton.h = 22.0f;

		if(DoButton_Menu(&Item.m_ActionButton, pActionLabel, ActionState, &ActionButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			if(InstallingThisItem)
			{
				AxisClientShopCancelInstall();
				break;
			}
			else if(!Selected)
			{
				if(Installed)
				{
					AxisClientShopApplyAsset(this, gs_AxisClientShopState.m_Tab, aAssetName, false);
					char aMessage[256];
					str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Applied"), aAssetName);
					AxisClientShopSetStatus(aMessage);
				}
				else
				{
					AxisClientShopStartInstall(this, gs_AxisClientShopState.m_Tab, Item);
					break;
				}
			}
		}

		if(Installed)
		{
			if(DoButton_Menu(&Item.m_DeleteButton, Localize("Delete"), 0, &DeleteButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.25f, 0.05f, 0.05f, 0.35f)))
			{
				if(AxisClientShopDeleteAsset(this, gs_AxisClientShopState.m_Tab, aAssetName))
				{
					char aMessage[256];
					str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Deleted"), aAssetName);
					AxisClientShopSetStatus(aMessage);
					break;
				}
				else
				{
					char aMessage[256];
					str_format(aMessage, sizeof(aMessage), "%s: %s", Localize("Unable to delete asset"), aAssetName);
					AxisClientShopSetStatus(aMessage);
					break;
				}
			}
		}
	}

	gs_AxisClientShopState.m_SelectedIndex = s_ListBox.DoEnd();

	static CButtonContainer s_aShopTabs[NUM_AXIS_SHOP_TABS] = {};
	const float TabWidth = TabsRow.w < (float)NUM_AXIS_SHOP_TABS * AXIS_SHOP_TAB_WIDTH ? TabsRow.w / (float)NUM_AXIS_SHOP_TABS : AXIS_SHOP_TAB_WIDTH;
	const float TabsWidth = (float)NUM_AXIS_SHOP_TABS * TabWidth;
	CUIRect Tabs = TabsRow;
	if(TabsRow.w > TabsWidth)
	{
		const float SideSpace = (TabsRow.w - TabsWidth) * 0.5f;
		TabsRow.VSplitLeft(SideSpace, nullptr, &Tabs);
		Tabs.VSplitLeft(TabsWidth, &Tabs, nullptr);
	}
	for(int Tab = 0; Tab < NUM_AXIS_SHOP_TABS; ++Tab)
	{
		CUIRect Button;
		Tabs.VSplitLeft(TabWidth, &Button, &Tabs);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == NUM_AXIS_SHOP_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aShopTabs[Tab], Localize(gs_aAxisClientShopTypeInfos[Tab].m_pLabel), gs_AxisClientShopState.m_Tab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			AxisClientShopSetTab(this, Tab);
		}
	}

	if(AxisClientShopHasPreviewOpen())
	{
		AxisClientShopRenderPreview(this, FullView);
	}
}
