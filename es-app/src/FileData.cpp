#include "FileData.h"
#include "SystemData.h"
#include "SystemManager.h"
#include "Settings.h"

namespace fs = boost::filesystem;

MetaDataListType fileTypeToMetaDataType(FileType type)
{
	switch(type)
	{
	case GAME:
		return GAME_METADATA;
	case FOLDER:
		return FOLDER_METADATA;
	case FILTER:
		return FILTER_METADATA;
	}

	return GAME_METADATA;
}

std::string removeParenthesis(const std::string& str)
{
	// remove anything in parenthesis or brackets
	// should be roughly equivalent to the regex replace "\((.*)\)|\[(.*)\]" with ""
	// I would love to just use regex, but it's not worth pulling in another boost lib for one function that is used once

	std::string ret = str;
	size_t start, end;

	static const int NUM_TO_REPLACE = 2;
	static const char toReplace[NUM_TO_REPLACE*2] = { '(', ')', '[', ']' };

	bool done = false;
	while(!done)
	{
		done = true;
		for(int i = 0; i < NUM_TO_REPLACE; i++)
		{
			end = ret.find_first_of(toReplace[i*2+1]);
			start = ret.find_last_of(toReplace[i*2], end);

			if(start != std::string::npos && end != std::string::npos)
			{
				ret.erase(start, end - start + 1);
				done = false;
			}
		}
	}

	// also strip whitespace
	end = ret.find_last_not_of(' ');
	if(end != std::string::npos)
		end++;

	ret = ret.substr(0, end);

	return ret;
}

std::string getCleanGameName(const std::string& str, const SystemData* system)
{
	fs::path path(str);
	std::string stem = path.stem().generic_string();
	if(system && system->hasPlatformId(PlatformIds::ARCADE) || system->hasPlatformId(PlatformIds::NEOGEO))
		stem = PlatformIds::getCleanMameName(stem.c_str());

	return stem;
}

FileData::FileData(const std::string& fileID, SystemData* system, FileType type, const std::string& nameCache)
	: mFileID(fileID), mSystem(system), mType(type), mNameCache(nameCache), mMetaDataCache(fileTypeToMetaDataType(type), false)
{
}

FileData::FileData() : FileData("", NULL, (FileType)0)
{

}

FileData::FileData(const std::string& fileID, const std::string& systemID, FileType type) : 
	FileData(fileID, SystemManager::getInstance()->getSystemByName(systemID), type)
{
}

const std::string& FileData::getSystemID() const
{
	return mSystem->getName();
}

const std::string& FileData::getName() const
{
	// try and cache what's in the DB
	if(mNameCache.empty())
		mNameCache = get_metadata().get<std::string>("name");

	// nothing was in the DB...use the clean version of our path
	if(mNameCache.empty())
		mNameCache = getCleanName();

	return mNameCache;
}

fs::path FileData::getPath() const
{
	return fileIDToPath(mFileID, mSystem);
}

FileType FileData::getType() const
{
	return mType;
}

MetaDataMap FileData::get_metadata() const
{
	if(mValidMetaDataCache) return mMetaDataCache;
	mMetaDataCache = SystemManager::getInstance()->database().getFileData(mFileID, mSystem->getName());
	mValidMetaDataCache = true;
	return mMetaDataCache;
}

void FileData::set_metadata(const MetaDataMap& metadata) const
{
	// The database round trip might alter some values.
	// Setting metadata should be fairly infrequent, so this should be fine.
	mValidMetaDataCache = false;
	SystemManager::getInstance()->database().setFileData(mFileID, getSystemID(), mType, metadata);
}

std::vector<FileData> FileData::getChildren(const FileSort* sort) const
{
	if(sort == NULL)
		sort = &getFileSorts().at(Settings::getInstance()->getInt("SortTypeIndex"));

	bool foldersFirst = Settings::getInstance()->getBool("SortFoldersFirst");

	if(mType == FILTER)
	{
		MetaDataMap metadata = get_metadata();
		//Sorry about abusing columns for other purposes.
		std::string filter_matches = mMetaDataCache.get("genre");
		int limit = mMetaDataCache.get<int>("players");
		//TODO: Let a filter also specify ability to match filters/folders
		std::string orderby = mMetaDataCache.get("developer");
		FileSort filterSort("Filter given sort",orderby.c_str());
		if(!orderby.empty())
			sort = &filterSort;
		return SystemManager::getInstance()->database().getChildrenOfFilter(mFileID, mSystem, false, filter_matches, limit, foldersFirst, sort);
	}

	return SystemManager::getInstance()->database().getChildrenOf(mFileID, mSystem, true, true, foldersFirst, sort);
}

std::vector<FileData> FileData::getChildrenRecursive(bool includeFolders, const FileSort* sort) const
{
	if(sort == NULL)
		sort = &getFileSorts().at(Settings::getInstance()->getInt("SortTypeIndex"));

	bool foldersFirst = Settings::getInstance()->getBool("SortFoldersFirst");

	if(mType == FILTER)
		return getChildren(sort);
	return SystemManager::getInstance()->database().getChildrenOf(mFileID, mSystem, false, includeFolders, foldersFirst, sort);
}
