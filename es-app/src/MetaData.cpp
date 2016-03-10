#include "MetaData.h"
#include <boost/assign.hpp>


#define MANDATORY_METADATA \
{"name",MD_STRING,"",false,"name","enter display name"},\
{"desc",MD_MULTILINE_STRING,"",false,"description","enter description"},\
{"image",MD_IMAGE_PATH,"",false,"image","enter path to image"},\
{"thumbnail",MD_IMAGE_PATH,"",false,"thumbnail","enter path to thumbnail"},

//because of how the database is set up, every entry is assumed to have at least the above metadata
//so they are stored in the filelist instead of the more specialized metadata tables.
//(this allows populating the system lists without joining multiple tables)

MetaDataDecl gameDecls[] = { 
	// key,			type,					default,			statistic,	name in GuiMetaDataEd,	prompt in GuiMetaDataEd
	MANDATORY_METADATA
	{"rating",		MD_RATING,				"0.000000", 		false,		"rating",				"enter rating"},
	{"releasedate", MD_DATE,				"not-a-date-time", 	false,		"release date",			"enter release date"},
	{"developer",	MD_STRING,				"unknown",			false,		"developer",			"enter game developer"},
	{"publisher",	MD_STRING,				"unknown",			false,		"publisher",			"enter game publisher"},
	{"genre",		MD_STRING,				"unknown",			false,		"genre",				"enter game genre"},
	{"players",		MD_INT,					"1",				false,		"players",				"enter number of players"},
	{"playcount",	MD_INT,					"0",				true,		"play count",			"enter number of times played"},
	{"lastplayed",	MD_TIME,				"not-a-date-time", 				true,		"last played",			"enter last played date"}
};


MetaDataDecl folderDecls[] = { 
	MANDATORY_METADATA
};

MetaDataDecl filterDecls[] = { 
	MANDATORY_METADATA
        {"query",	MD_MULTILINE_STRING,			"rating > .6 AND playcount > 0", 				false,		"query",			"enter query"},
	{"ordering",	MD_STRING,				"",			false,		"order by",			"enter columns to order by"},
	{"maxcount",		MD_INT,					"0",				false,		"limit",				"enter limit on results"}
};

std::map< MetaDataListType, std::vector<MetaDataDecl> > MDD_map = boost::assign::map_list_of
	(GAME_METADATA, 
		std::vector<MetaDataDecl>(gameDecls, gameDecls + sizeof(gameDecls) / sizeof(gameDecls[0])))
	(FOLDER_METADATA, 
		std::vector<MetaDataDecl>(folderDecls, folderDecls + sizeof(folderDecls) / sizeof(folderDecls[0])))
	(FILTER_METADATA, 
		std::vector<MetaDataDecl>(filterDecls, filterDecls + sizeof(filterDecls) / sizeof(filterDecls[0])));

const std::map<MetaDataListType, std::vector<MetaDataDecl> >& getMDDMap()
{
	return MDD_map;
}

MetaDataMap::MetaDataMap(MetaDataListType type)
	: mType(type)
{
	setDefaults();
}
MetaDataMap::MetaDataMap(MetaDataListType type, bool init)
	: mType(type)
{
	if(init) setDefaults();
}

void MetaDataMap::setDefaults()
{
	//To enforce that subset constraint above, we intialize the map to have
	//all the defaults of the gameDecls, with our specific defaults overriding.
	const std::vector<MetaDataDecl>& mddGame = getMDDMap().at(GAME_METADATA);
	const std::vector<MetaDataDecl>& mdd = getMDD();
	for(auto iter = mddGame.begin(); iter != mddGame.end(); iter++)
		set(iter->key, iter->defaultValue); 
	if(mType == GAME_METADATA) return;
	for(auto iter = mdd.begin(); iter != mdd.end(); iter++)
		set(iter->key, iter->defaultValue);
}
