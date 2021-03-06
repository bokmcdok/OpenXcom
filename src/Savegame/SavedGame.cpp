/*
 * Copyright 2010-2013 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "SavedGame.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <yaml-cpp/yaml.h>
#include "../version.h"
#include "../Engine/Logger.h"
#include "../Ruleset/Ruleset.h"
#include "../Engine/RNG.h"
#include "../Engine/Language.h"
#include "../Interface/TextList.h"
#include "../Engine/Exception.h"
#include "../Engine/Options.h"
#include "../Engine/CrossPlatform.h"
#include "SavedBattleGame.h"
#include "GameTime.h"
#include "Country.h"
#include "Base.h"
#include "Craft.h"
#include "Region.h"
#include "Ufo.h"
#include "Waypoint.h"
#include "../Ruleset/RuleResearch.h"
#include "ResearchProject.h"
#include "ItemContainer.h"
#include "Soldier.h"
#include "../Ruleset/RuleManufacture.h"
#include "Production.h"
#include "TerrorSite.h"
#include "AlienBase.h"
#include "AlienStrategy.h"
#include "AlienMission.h"
#include "../Ruleset/RuleRegion.h"

namespace OpenXcom
{
struct findRuleResearch : public std::unary_function<ResearchProject *,
								bool>
{
	RuleResearch * _toFind;
	findRuleResearch(RuleResearch * toFind);
	bool operator()(const ResearchProject *r) const;
};

findRuleResearch::findRuleResearch(RuleResearch * toFind) : _toFind(toFind)
{
}

bool findRuleResearch::operator()(const ResearchProject *r) const
{
	return _toFind == r->getRules();
}

struct equalProduction : public std::unary_function<Production *,
							bool>
{
	RuleManufacture * _item;
	equalProduction(RuleManufacture * item);
	bool operator()(const Production * p) const;
};

equalProduction::equalProduction(RuleManufacture * item) : _item(item)
{
}

bool equalProduction::operator()(const Production * p) const
{
	return p->getRules() == _item;
}

/**
 * Initializes a brand new saved game according to the specified difficulty.
 */
SavedGame::SavedGame() : _difficulty(DIFF_BEGINNER), _globeLon(0.0), _globeLat(0.0), _globeZoom(0), _battleGame(0), _debug(false), _warned(false), _detail(true), _radarLines(false), _monthsPassed(-1), _graphRegionToggles(""), _graphCountryToggles(""), _graphFinanceToggles("")
{
	RNG::init();
	_time = new GameTime(6, 1, 1, 1999, 12, 0, 0);
	_alienStrategy = new AlienStrategy();
	_funds.push_back(0);
	_maintenance.push_back(0);
	_researchScores.push_back(0);
}

/**
 * Deletes the game content from memory.
 */
SavedGame::~SavedGame()
{
	delete _time;
	for (std::vector<Country*>::iterator i = _countries.begin(); i != _countries.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Region*>::iterator i = _regions.begin(); i != _regions.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Base*>::iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Ufo*>::iterator i = _ufos.begin(); i != _ufos.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Waypoint*>::iterator i = _waypoints.begin(); i != _waypoints.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<TerrorSite*>::iterator i = _terrorSites.begin(); i != _terrorSites.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<AlienBase*>::iterator i = _alienBases.begin(); i != _alienBases.end(); ++i)
 	{
		delete *i;
	}
	delete _alienStrategy;
	for (std::vector<AlienMission*>::iterator i = _activeMissions.begin(); i != _activeMissions.end(); ++i)
	{
		delete *i;
	}
	delete _battleGame;
}

/**
 * Gets all the saves found in the user folder
 * and adds them to a text list.
 * @param list Text list.
 * @param lang Loaded language.
 */
void SavedGame::getList(TextList *list, Language *lang)
{
	std::vector<std::string> saves = CrossPlatform::getFolderContents(Options::getUserFolder(), "sav");

	for (std::vector<std::string>::iterator i = saves.begin(); i != saves.end(); ++i)
	{
		std::string file = (*i);
		std::string fullname = Options::getUserFolder() + file;
		try
		{
			YAML::Node doc = YAML::LoadFile(fullname);
			GameTime time = GameTime(6, 1, 1, 1999, 12, 0, 0);
			time.load(doc["time"]);
			std::stringstream saveTime;
			std::wstringstream saveDay, saveMonth, saveYear;
			saveTime << time.getHour() << ":" << std::setfill('0') << std::setw(2) << time.getMinute();
			saveDay << time.getDay() << lang->getString(time.getDayString());
			saveMonth << lang->getString(time.getMonthString());
			saveYear << time.getYear();

			std::string s = file.substr(0, file.length()-4);
#ifdef _WIN32
			std::wstring wstr = Language::cpToWstr(s);
#else
			std::wstring wstr = Language::utf8ToWstr(s);
#endif
			list->addRow(5, wstr.c_str(), Language::utf8ToWstr(saveTime.str()).c_str(), saveDay.str().c_str(), saveMonth.str().c_str(), saveYear.str().c_str());
		}
		catch (Exception &e)
		{
			Log(LOG_ERROR) << e.what();
			continue;
		}
		catch (YAML::Exception &e)
		{
			Log(LOG_ERROR) << e.what();
			continue;
		}
	}
}

/**
 * Loads a saved game's contents from a YAML file.
 * @note Assumes the saved game is blank.
 * @param filename YAML filename.
 * @param rule Ruleset for the saved game.
 */
void SavedGame::load(const std::string &filename, Ruleset *rule)
{
	std::string s = Options::getUserFolder() + filename + ".sav";
	std::vector<YAML::Node> file = YAML::LoadAllFromFile(s);
	if (file.empty())
	{
		throw Exception(filename + " is not a vaild save file");
	}

	// Get brief save info
	YAML::Node brief = file[0];
	std::string version = brief["version"].as<std::string>();
	if (version != OPENXCOM_VERSION_SHORT)
	{
		throw Exception("Version mismatch");
	}
	_time->load(brief["time"]);

	// Get full save data
	YAML::Node doc = file[1];
	_difficulty = (GameDifficulty)doc["difficulty"].as<int>(_difficulty);
	RNG::load(doc);
	_monthsPassed = doc["monthsPassed"].as<int>(_monthsPassed);
	_radarLines = doc["radarLines"].as<bool>(_radarLines);
	_detail = doc["detail"].as<bool>(_detail);
	_graphRegionToggles = doc["GraphRegionToggles"].as<std::string>(_graphRegionToggles);
	_graphCountryToggles = doc["GraphCountryToggles"].as<std::string>(_graphCountryToggles);
	_graphFinanceToggles = doc["GraphFinanceToggles"].as<std::string>(_graphFinanceToggles);
	_funds = doc["funds"].as< std::vector<int> >(_funds);
	_maintenance = doc["maintenance"].as< std::vector<int> >(_maintenance);
	_researchScores = doc["researchScores"].as< std::vector<int> >(_researchScores);
	_warned = doc["warned"].as<bool>(_warned);
	_globeLon = doc["globeLon"].as<double>(_globeLon);
	_globeLat = doc["globeLat"].as<double>(_globeLat);
	_globeZoom = doc["globeZoom"].as<int>(_globeZoom);
	initIds(doc["ids"].as< std::map<std::string, int> >(_ids));

	for (YAML::const_iterator i = doc["countries"].begin(); i != doc["countries"].end(); ++i)
	{
		std::string type = (*i)["type"].as<std::string>();
		Country *c = new Country(rule->getCountry(type), false);
		c->load(*i);
		_countries.push_back(c);
	}

	for (YAML::const_iterator i = doc["regions"].begin(); i != doc["regions"].end(); ++i)
	{
		std::string type = (*i)["type"].as<std::string>();
		Region *r = new Region(rule->getRegion(type));
		r->load(*i);
		_regions.push_back(r);
	}

	// Alien bases must be loaded before alien missions
	for (YAML::const_iterator i = doc["alienBases"].begin(); i != doc["alienBases"].end(); ++i)
	{
		AlienBase *b = new AlienBase();
		b->load(*i);
		_alienBases.push_back(b);
	}

	// Missions must be loaded before UFOs.
	const YAML::Node &missions = doc["alienMissions"];
	for (YAML::const_iterator it = missions.begin(); it != missions.end(); ++it)
	{
		std::string missionType = (*it)["type"].as<std::string>();
		const RuleAlienMission &mRule = *rule->getAlienMission(missionType);
		std::auto_ptr<AlienMission> mission(new AlienMission(mRule));
		mission->load(*it, *this);
		_activeMissions.push_back(mission.release());
	}

	for (YAML::const_iterator i = doc["ufos"].begin(); i != doc["ufos"].end(); ++i)
	{
		std::string type = (*i)["type"].as<std::string>();
		Ufo *u = new Ufo(rule->getUfo(type));
		u->load(*i, *rule, *this);
		_ufos.push_back(u);
	}

	for (YAML::const_iterator i = doc["waypoints"].begin(); i != doc["waypoints"].end(); ++i)
	{
		Waypoint *w = new Waypoint();
		w->load(*i);
		_waypoints.push_back(w);
	}

	for (YAML::const_iterator i = doc["terrorSites"].begin(); i != doc["terrorSites"].end(); ++i)
	{
		TerrorSite *t = new TerrorSite();
		t->load(*i);
		_terrorSites.push_back(t);
	}

	for (YAML::const_iterator i = doc["bases"].begin(); i != doc["bases"].end(); ++i)
	{
		Base *b = new Base(rule);
		b->load(*i, this, false);
		_bases.push_back(b);
	}

	for(YAML::const_iterator it = doc["discovered"].begin(); it != doc["discovered"].end(); ++it)
	{
		std::string research = it->as<std::string>();
		_discovered.push_back(rule->getResearch(research));
	}
	
	const YAML::Node &research = doc["poppedResearch"];
	for(YAML::const_iterator it = research.begin(); it != research.end(); ++it)
	{
		std::string research = it->as<std::string>();
		_poppedResearch.push_back(rule->getResearch(research));
	}
	_alienStrategy->load(rule, doc["alienStrategy"]);

	if (const YAML::Node &battle = doc["battleGame"])
	{
		_battleGame = new SavedBattleGame();
		_battleGame->load(battle, rule, this);
	}
}

/**
 * Saves a saved game's contents to a YAML file.
 * @param filename YAML filename.
 */
void SavedGame::save(const std::string &filename) const
{
	std::string s = Options::getUserFolder() + filename + ".sav";
	std::ofstream sav(s.c_str());
	if (!sav)
	{
		throw Exception("Failed to save " + filename + ".sav");
	}

	YAML::Emitter out;

	// Saves the brief game info used in the saves list
	YAML::Node brief;
	brief["version"] = OPENXCOM_VERSION_SHORT;
	brief["build"] = OPENXCOM_VERSION_GIT;
	brief["time"] = _time->save();
	out << brief;
	// Saves the full game data to the save
	out << YAML::BeginDoc;
	YAML::Node node;
	node["difficulty"] = (int)_difficulty;
	node["monthsPassed"] = _monthsPassed;
	node["radarLines"] = _radarLines;
	node["detail"] = _detail;
	node["GraphRegionToggles"] = _graphRegionToggles;
	node["GraphCountryToggles"] = _graphCountryToggles;
	node["GraphFinanceToggles"] = _graphFinanceToggles;
	RNG::save(node);
	node["funds"] = _funds;
	node["maintenance"] = _maintenance;
	node["researchScores"] = _researchScores;
	node["warned"] = _warned;
	node["globeLon"] = _globeLon;
	node["globeLat"] = _globeLat;
	node["globeZoom"] = _globeZoom;
	node["ids"] = _ids;
	for (std::vector<Country*>::const_iterator i = _countries.begin(); i != _countries.end(); ++i)
	{
		node["countries"].push_back((*i)->save());
	}
	for (std::vector<Region*>::const_iterator i = _regions.begin(); i != _regions.end(); ++i)
	{
		node["regions"].push_back((*i)->save());
	}
	for (std::vector<Base*>::const_iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		node["bases"].push_back((*i)->save());
	}
	for (std::vector<Waypoint*>::const_iterator i = _waypoints.begin(); i != _waypoints.end(); ++i)
	{
		node["waypoints"].push_back((*i)->save());
	}
	for (std::vector<TerrorSite*>::const_iterator i = _terrorSites.begin(); i != _terrorSites.end(); ++i)
	{
		node["terrorSites"].push_back((*i)->save());
	}
	// Alien bases must be saved before alien missions.
	for (std::vector<AlienBase*>::const_iterator i = _alienBases.begin(); i != _alienBases.end(); ++i)
	{
		node["alienBases"].push_back((*i)->save());
	}
	// Missions must be saved before UFOs, but after alien bases.
	for (std::vector<AlienMission *>::const_iterator i = _activeMissions.begin(); i != _activeMissions.end(); ++i)
	{
		node["alienMissions"].push_back((*i)->save());
	}
	// UFOs must be after missions
	for (std::vector<Ufo*>::const_iterator i = _ufos.begin(); i != _ufos.end(); ++i)
	{
		node["ufos"].push_back((*i)->save());
	}
	for (std::vector<const RuleResearch *>::const_iterator i = _discovered.begin(); i != _discovered.end(); ++i)
	{
		node["discovered"].push_back((*i)->getName());
	}
	for (std::vector<const RuleResearch *>::const_iterator i = _poppedResearch.begin(); i != _poppedResearch.end(); ++i)
	{
		node["poppedResearch"].push_back((*i)->getName());
	}
	node["alienStrategy"] = _alienStrategy->save();
	if (_battleGame != 0)
	{
		node["battleGame"] = _battleGame->save();
	}
	out << node;
	sav << out.c_str();
	sav.close();
}

/**
 * Returns the game's difficulty level.
 * @return Difficulty level.
 */
GameDifficulty SavedGame::getDifficulty() const
{
	return _difficulty;
}

/**
 * Changes the game's difficulty to a new level.
 * @param difficulty New difficulty.
 */
void SavedGame::setDifficulty(GameDifficulty difficulty)
{
	_difficulty = difficulty;
}

/**
 * Returns the player's current funds.
 * @return Current funds.
 */
int SavedGame::getFunds() const
{
	return _funds.back();
}

/**
 * Returns the player's funds for the last 12 months.
 * @return funds.
 */
const std::vector<int> &SavedGame::getFundsList() const
{
	return _funds;
}

/**
 * Changes the player's funds to a new value.
 * @param funds New funds.
 */
void SavedGame::setFunds(int funds)
{
	_funds.back() = funds;
}

/**
 * Returns the current longitude of the Geoscape globe.
 * @return Longitude.
 */
double SavedGame::getGlobeLongitude() const
{
	return _globeLon;
}

/**
 * Changes the current longitude of the Geoscape globe.
 * @param lon Longitude.
 */
void SavedGame::setGlobeLongitude(double lon)
{
	_globeLon = lon;
}

/**
 * Returns the current latitude of the Geoscape globe.
 * @return Latitude.
 */
double SavedGame::getGlobeLatitude() const
{
	return _globeLat;
}

/**
 * Changes the current latitude of the Geoscape globe.
 * @param lat Latitude.
 */
void SavedGame::setGlobeLatitude(double lat)
{
	_globeLat = lat;
}

/**
 * Returns the current zoom level of the Geoscape globe.
 * @return Zoom level.
 */
int SavedGame::getGlobeZoom() const
{
	return _globeZoom;
}

/**
 * Changes the current zoom level of the Geoscape globe.
 * @param zoom Zoom level.
 */
void SavedGame::setGlobeZoom(int zoom)
{
	_globeZoom = zoom;
}

/**
 * Gives the player his monthly funds, taking in account
 * all maintenance and profit costs.
 */
void SavedGame::monthlyFunding()
{
	_funds.back() += getCountryFunding() - getBaseMaintenance();
	_funds.push_back(_funds.back());
	_maintenance.back() = getBaseMaintenance();
	_maintenance.push_back(0);

	_researchScores.push_back(0);
	if (_researchScores.size() > 12)
		_researchScores.erase(_researchScores.begin());

	if(_funds.size() > 12)
		_funds.erase(_funds.begin());
	if(_maintenance.size() > 12)
		_maintenance.erase(_maintenance.begin());
}

/**
 * Returns the current time of the game.
 * @return Pointer to the game time.
 */
GameTime *SavedGame::getTime() const
{
	return _time;
}

/**
 * Changes the current time of the game.
 * @param time Game time.
 */
void SavedGame::setTime(GameTime time)
{
	_time = new GameTime(time);
}

/**
 * Returns the latest ID for the specified object
 * and increases it.
 * @param name Object name.
 * @return Latest ID number.
 */
int SavedGame::getId(const std::string &name)
{
	int id = _ids[name];
	_ids[name]++;
	return id;
}

/**
 * Initializes the list of object IDs.
 * @param ids ID number list.
 */
void SavedGame::initIds(const std::map<std::string, int> &ids)
{
	_ids["STR_UFO"] = 1;
	_ids["STR_LANDING_SITE"] = 1;
	_ids["STR_CRASH_SITE"] = 1;
	_ids["STR_WAYPOINT"] = 1;
	_ids["STR_TERROR_SITE"] = 1;
	_ids["STR_ALIEN_BASE"] = 1;
	_ids["STR_SOLDIER"] = 1;
	_ids["ALIEN_MISSIONS"] = 1;
	for (std::map<std::string, int>::const_iterator i = ids.begin(); i != ids.end(); ++i)
	{
		_ids[i->first] = i->second;
	}
}

/**
 * Returns the list of countries in the game world.
 * @return Pointer to country list.
 */
std::vector<Country*> *SavedGame::getCountries()
{
	return &_countries;
}

/**
 * Adds up the monthly funding of all the countries.
 * @return Total funding.
 */
int SavedGame::getCountryFunding() const
{
	int total = 0;
	for (std::vector<Country*>::const_iterator i = _countries.begin(); i != _countries.end(); ++i)
	{
		total += (*i)->getFunding().back();
	}
	return total;
}

/**
 * Returns the list of world regions.
 * @return Pointer to region list.
 */
std::vector<Region*> *SavedGame::getRegions()
{
	return &_regions;
}

/**
 * Returns the list of player bases.
 * @return Pointer to base list.
 */
std::vector<Base*> *SavedGame::getBases()
{
	return &_bases;
}

/**
 * Returns an immutable list of player bases.
 * @return Pointer to base list.
 */
const std::vector<Base*> *SavedGame::getBases() const
{
	return &_bases;
}

/**
 * Adds up the monthly maintenance of all the bases.
 * @return Total maintenance.
 */
int SavedGame::getBaseMaintenance() const
{
	int total = 0;
	for (std::vector<Base*>::const_iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		total += (*i)->getMonthlyMaintenace();
	}
	return total;
}

/**
 * Returns the list of alien UFOs.
 * @return Pointer to UFO list.
 */
std::vector<Ufo*> *SavedGame::getUfos()
{
	return &_ufos;
}

/**
 * Returns the list of craft waypoints.
 * @return Pointer to waypoint list.
 */
std::vector<Waypoint*> *SavedGame::getWaypoints()
{
	return &_waypoints;
}

/**
 * Returns the list of terror sites.
 * @return Pointer to terror site list.
 */
std::vector<TerrorSite*> *SavedGame::getTerrorSites()
{
	return &_terrorSites;
}

/**
 * Get pointer to the battleGame object.
 * @return Pointer to the battleGame object.
 */
SavedBattleGame *SavedGame::getSavedBattle()
{
	return _battleGame;
}

/**
 * Set battleGame object.
 * @param battleGame Pointer to the battleGame object.
 */
void SavedGame::setBattleGame(SavedBattleGame *battleGame)
{
	delete _battleGame;
	_battleGame = battleGame;
}

/**
 * Add a ResearchProject to the list of already discovered ResearchProject
 * @param r The newly found ResearchProject
*/
void SavedGame::addFinishedResearch (const RuleResearch * r, const Ruleset * ruleset)
{
	std::vector<const RuleResearch *>::const_iterator itDiscovered = std::find(_discovered.begin (), _discovered.end (), r);
	if(itDiscovered == _discovered.end())
	{
		_discovered.push_back(r);
		removePoppedResearch(r);
		addResearchScore(r->getPoints());
	}
	if(ruleset)
	{
		std::vector<RuleResearch*> availableResearch;
		for(std::vector<Base*>::const_iterator it = _bases.begin (); it != _bases.end (); ++it)
		{
			getDependableResearchBasic(availableResearch, r, ruleset, *it);
		}
		for(std::vector<RuleResearch*>::iterator it = availableResearch.begin (); it != availableResearch.end (); ++it)
		{
			if((*it)->getCost() == 0 && (*it)->getRequirements().empty())
			{
				addFinishedResearch(*it, ruleset);
			}
			else if((*it)->getCost() == 0)
			{
				int entry(0);
				for(std::vector<std::string>::const_iterator iter = (*it)->getRequirements().begin (); iter != (*it)->getRequirements().end (); ++iter)
				{
					if((*it)->getRequirements().at(entry) == (*iter))
					{
						addFinishedResearch(*it, ruleset);
					}
					entry++;
				}
			}
		}
	}
}

/**
   Returns the list of already discovered ResearchProject
 * @return the list of already discovered ResearchProject
*/
const std::vector<const RuleResearch *> & SavedGame::getDiscoveredResearch() const
{
	return _discovered;
}

/**
   Get the list of RuleResearch which can be researched in a Base.
   * @param projects the list of ResearchProject which are available.
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getAvailableResearchProjects (std::vector<RuleResearch *> & projects, const Ruleset * ruleset, Base * base) const
{
	const std::vector<const RuleResearch *> & discovered(getDiscoveredResearch());
	std::vector<std::string> researchProjects = ruleset->getResearchList();
	const std::vector<ResearchProject *> & baseResearchProjects = base->getResearch();
	std::vector<const RuleResearch *> unlocked;
	for(std::vector<const RuleResearch *>::const_iterator it = discovered.begin (); it != discovered.end (); ++it)
	{
		for(std::vector<std::string>::const_iterator itUnlocked = (*it)->getUnlocked ().begin (); itUnlocked != (*it)->getUnlocked ().end (); ++itUnlocked)
		{
			unlocked.push_back(ruleset->getResearch(*itUnlocked));
		}
	}
	for(std::vector<std::string>::const_iterator iter = researchProjects.begin (); iter != researchProjects.end (); ++iter)
	{
		RuleResearch *research = ruleset->getResearch(*iter);
		if (!isResearchAvailable(research, unlocked, ruleset))
		{
			continue;
		}
		std::vector<const RuleResearch *>::const_iterator itDiscovered = std::find(discovered.begin (), discovered.end (), research);
		
		bool liveAlien = ruleset->getUnit(research->getName()) != 0;

		if (itDiscovered != discovered.end ())
		{
			if (!liveAlien)
			{
				continue;
			}
			else
			{
				bool cull = true;
				if (research->getGetOneFree().size() != 0)
				{
					for (std::vector<std::string>::const_iterator ohBoy = research->getGetOneFree().begin(); ohBoy != research->getGetOneFree().end(); ++ohBoy)
					{
						std::vector<const RuleResearch *>::const_iterator more_iteration = std::find(discovered.begin (), discovered.end (), ruleset->getResearch(*ohBoy));
						if (more_iteration == discovered.end ())
						{
							cull = false;
							break;
						}
					}
				}
				std::vector<std::string>::const_iterator leaderCheck = std::find(research->getUnlocked().begin(), research->getUnlocked().end(), "STR_LEADER_PLUS");
				std::vector<std::string>::const_iterator cmnderCheck = std::find(research->getUnlocked().begin(), research->getUnlocked().end(), "STR_CYDONIA_DEP");
				
				bool leader ( leaderCheck != research->getUnlocked().end());
				bool cmnder ( cmnderCheck != research->getUnlocked().end());

				if (leader)
				{
					std::vector<const RuleResearch*>::const_iterator found = std::find(discovered.begin(), discovered.end(), ruleset->getResearch("STR_LEADER_PLUS"));
					if (found == discovered.end())
						cull = false;
				}

				if (cmnder)
				{
					std::vector<const RuleResearch*>::const_iterator found = std::find(discovered.begin(), discovered.end(), ruleset->getResearch("STR_CYDONIA_DEP"));
					if (found == discovered.end())
						cull = false;
				}

				if (cull)
					continue;
			}
		}

		if (std::find_if (baseResearchProjects.begin(), baseResearchProjects.end (), findRuleResearch(research)) != baseResearchProjects.end ())
		{
			continue;
		}
		if (research->needItem() && base->getItems()->getItem(research->getName ()) == 0)
		{
			continue;
		}
		if (research->getRequirements().size() != 0)
		{
			size_t tally(0);
			for(size_t itreq = 0; itreq != research->getRequirements().size(); ++itreq)
			{
				itDiscovered = std::find(discovered.begin (), discovered.end (), ruleset->getResearch(research->getRequirements().at(itreq)));
				if (itDiscovered != discovered.end ())
				{
					tally++;
				}
			}
			if(tally != research->getRequirements().size())
			continue;
		}
		projects.push_back (research);
	}
}

/**
   Get the list of RuleManufacture which can be manufacture in a Base.
   * @param productions the list of Productions which are available.
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getAvailableProductions (std::vector<RuleManufacture *> & productions, const Ruleset * ruleset, Base * base) const
{
	const std::vector<std::string> &items = ruleset->getManufactureList ();
	const std::vector<Production *> baseProductions (base->getProductions ());

	for(std::vector<std::string>::const_iterator iter = items.begin ();
		iter != items.end ();
		++iter)
	{
		RuleManufacture *m = ruleset->getManufacture(*iter);
		if(!isResearched(m->getRequirements()))
		{
		 	continue;
		}
		if(std::find_if(baseProductions.begin (), baseProductions.end (), equalProduction(m)) != baseProductions.end ())
		{
			continue;
		}
		productions.push_back(m);
	}
}

/**
   Check whether a ResearchProject can be researched.
   * @param r the RuleResearch to test.
   * @param unlocked the list of currently unlocked RuleResearch
   * @return true if the RuleResearch can be researched
*/
bool SavedGame::isResearchAvailable (RuleResearch * r, const std::vector<const RuleResearch *> & unlocked, const Ruleset * ruleset) const
{
	std::vector<std::string> deps = r->getDependencies();
	const std::vector<const RuleResearch *> & discovered(getDiscoveredResearch());
	bool liveAlien = ruleset->getUnit(r->getName()) != 0;
	if(std::find(unlocked.begin (), unlocked.end (), r) != unlocked.end ())
	{
		return true;
	}
	else if (liveAlien)
	{		
		if (!r->getGetOneFree().empty())
		{
			for (std::vector<std::string>::const_iterator itFree = r->getGetOneFree().begin(); itFree != r->getGetOneFree().end(); ++itFree)
			{
				if(std::find(unlocked.begin (), unlocked.end (), ruleset->getResearch(*itFree)) == unlocked.end ())
				{
					return true;
				}
			}
			std::vector<std::string>::const_iterator leaderCheck = std::find(r->getUnlocked().begin(), r->getUnlocked().end(), "STR_LEADER_PLUS");
			std::vector<std::string>::const_iterator cmnderCheck = std::find(r->getUnlocked().begin(), r->getUnlocked().end(), "STR_CYDONIA_DEP");
				
			bool leader ( leaderCheck != r->getUnlocked().end());
			bool cmnder ( cmnderCheck != r->getUnlocked().end());

			if (leader)
			{
				std::vector<const RuleResearch*>::const_iterator found = std::find(discovered.begin(), discovered.end(), ruleset->getResearch("STR_LEADER_PLUS"));
				if (found == discovered.end())
					return true;
			}

			if (cmnder)
			{
				std::vector<const RuleResearch*>::const_iterator found = std::find(discovered.begin(), discovered.end(), ruleset->getResearch("STR_CYDONIA_DEP"));
				if (found == discovered.end())
					return true;
			}
		}
	}

	for(std::vector<std::string>::const_iterator iter = deps.begin (); iter != deps.end (); ++ iter)
	{
		RuleResearch *research = ruleset->getResearch(*iter);
		std::vector<const RuleResearch *>::const_iterator itDiscovered = std::find(discovered.begin (), discovered.end (), research);
		if (itDiscovered == discovered.end ())
		{
			return false;
		}
	}

	return true;
}

/**
   Get the list of newly available research projects once a ResearchProject has been completed. This function check for fake ResearchProject.
   * @param dependables the list of RuleResearch which are now available.
   * @param research The RuleResearch which has just been discovered
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getDependableResearch (std::vector<RuleResearch *> & dependables, const RuleResearch *research, const Ruleset * ruleset, Base * base) const
{
	getDependableResearchBasic(dependables, research, ruleset, base);
	for(std::vector<const RuleResearch *>::const_iterator iter = _discovered.begin (); iter != _discovered.end (); ++iter)
	{
		if((*iter)->getCost() == 0)
		{
			if (std::find((*iter)->getDependencies().begin (), (*iter)->getDependencies().end (), research->getName()) != (*iter)->getDependencies().end ())
			{
				getDependableResearchBasic(dependables, *iter, ruleset, base);
			}
		}
	}
}

/**
   Get the list of newly available research projects once a ResearchProject has been completed. This function doesn't check for fake ResearchProject.
   * @param dependables the list of RuleResearch which are now available.
   * @param research The RuleResearch which has just been discovered
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getDependableResearchBasic (std::vector<RuleResearch *> & dependables, const RuleResearch *research, const Ruleset * ruleset, Base * base) const
{
	std::vector<RuleResearch *> possibleProjects;
	getAvailableResearchProjects(possibleProjects, ruleset, base);
	for(std::vector<RuleResearch *>::iterator iter = possibleProjects.begin (); iter != possibleProjects.end (); ++iter)
	{
		if (std::find((*iter)->getDependencies().begin (), (*iter)->getDependencies().end (), research->getName()) != (*iter)->getDependencies().end ()
			|| std::find((*iter)->getUnlocked().begin (), (*iter)->getUnlocked().end (), research->getName()) != (*iter)->getUnlocked().end ())
		{
			dependables.push_back(*iter);
			if ((*iter)->getCost() == 0)
			{
				getDependableResearchBasic(dependables, *iter, ruleset, base);
			}
		}
	}
}

/**
   Get the list of newly available manufacture projects once a ResearchProject has been completed. This function check for fake ResearchProject.
   * @param dependables the list of RuleManufacture which are now available.
   * @param research The RuleResearch which has just been discovered
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getDependableManufacture (std::vector<RuleManufacture *> & dependables, const RuleResearch *research, const Ruleset * ruleset, Base *) const
{
	const std::vector<std::string> &mans = ruleset->getManufactureList();
	for(std::vector<std::string>::const_iterator iter = mans.begin (); iter != mans.end (); ++iter)
	{
		RuleManufacture *m = ruleset->getManufacture(*iter);
		const std::vector<std::string> &reqs = m->getRequirements();
		if(isResearched(m->getRequirements()) && std::find(reqs.begin(), reqs.end(), research->getName()) != reqs.end())
		{
			dependables.push_back(m);
		}
	}
}

/**
 * Returns if a certain research has been completed.
 * @param research Research ID.
 * @return Whether it's researched or not.
 */
bool SavedGame::isResearched(const std::string &research) const
{
	if (research.empty() || _debug)
		return true;
	for (std::vector<const RuleResearch *>::const_iterator i = _discovered.begin(); i != _discovered.end(); ++i)
	{
		if ((*i)->getName() == research)
			return true;
	}

	return false;
}

/**
 * Returns if a certain list of research has been completed.
 * @param research List of research IDs.
 * @return Whether it's researched or not.
 */
bool SavedGame::isResearched(const std::vector<std::string> &research) const
{
	if (research.empty() || _debug)
		return true;
	std::vector<std::string> matches = research;
	for (std::vector<const RuleResearch *>::const_iterator i = _discovered.begin(); i != _discovered.end(); ++i)
	{
		for (std::vector<std::string>::iterator j = matches.begin(); j != matches.end(); ++j)
		{
			if ((*i)->getName() == *j)
			{
				j = matches.erase(j);
				break;
			}
		}
		if (matches.empty())
			return true;
	}

	return false;
}

/**
 * Returns pointer to the Soldier given it's unique ID.
 * @param id A soldier's unique id.
 * @return Pointer to Soldier.
 */
Soldier *SavedGame::getSoldier(int id) const
{
	for (std::vector<Base*>::const_iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		for (std::vector<Soldier*>::const_iterator j = (*i)->getSoldiers()->begin(); j != (*i)->getSoldiers()->end(); ++j)
		{
			if ((*j)->getId() == id)
			{
				return (*j);
			}
		}
	}
	return 0;
}

/**
 * Handles the higher promotions (not the rookie-squaddie ones).
 * @return Whether or not some promotions happened - to show the promotions screen.
 */
bool SavedGame::handlePromotions()
{
	size_t soldiersPromoted = 0, soldiersTotal = 0;

	for (std::vector<Base*>::iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		soldiersTotal += (*i)->getSoldiers()->size();
	}
	Soldier *highestRanked = 0;

	// now determine the number of positions we have of each rank,
	// and the soldier with the heighest promotion score of the rank below it

	size_t filledPositions = 0, filledPositions2 = 0;
	inspectSoldiers(&highestRanked, &filledPositions, RANK_COMMANDER);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_COLONEL);
	if (filledPositions < 1 && filledPositions2 > 0)
	{
		// only promote one colonel to commander
		highestRanked->promoteRank();
		soldiersPromoted++;
	}
	inspectSoldiers(&highestRanked, &filledPositions, RANK_COLONEL);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_CAPTAIN);
	if (filledPositions < (soldiersTotal / 23) && filledPositions2 > 0)
	{
		highestRanked->promoteRank();
		soldiersPromoted++;
	}
	inspectSoldiers(&highestRanked, &filledPositions, RANK_CAPTAIN);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_SERGEANT);
	if (filledPositions < (soldiersTotal / 11) && filledPositions2 > 0)
	{
		highestRanked->promoteRank();
		soldiersPromoted++;
	}
	inspectSoldiers(&highestRanked, &filledPositions, RANK_SERGEANT);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_SQUADDIE);
	if (filledPositions < (soldiersTotal / 5) && filledPositions2 > 0)
	{
		highestRanked->promoteRank();
		soldiersPromoted++;
	}

	return (soldiersPromoted > 0);
}

/**
 * Checks how many soldiers of a rank exist and which one has the highest score.
 * @param Pointer to an int to store the total in.
 * @param Rank to inspect.
 * @return The highest scoring soldier of that rank.
 */
void SavedGame::inspectSoldiers(Soldier **highestRanked, size_t *total, int rank)
{
	int highestScore = 0;
	*total = 0;

	for (std::vector<Base*>::iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		for (std::vector<Soldier*>::iterator j = (*i)->getSoldiers()->begin(); j != (*i)->getSoldiers()->end(); ++j)
		{
			if ((*j)->getRank() == (SoldierRank)rank)
			{
				(*total)++;
				UnitStats *s = (*j)->getCurrentStats();
				int v1 = 2 * s->health + 2 * s->stamina + 4 * s->reactions + 4 * s->bravery;
				int v2 = v1 + 3*( s->tu + 2*( s->firing ) );
				int v3 = v2 + s->melee + s->throwing + s->strength;
				if (s->psiSkill > 0) v3 += s->psiStrength + 2 * s->psiSkill;
				int score = v3 + 10 * ( (*j)->getMissions() + (*j)->getKills() );
				if (score > highestScore)
				{
					highestScore = score;
					*highestRanked = (*j);
				}
			}
		}
	}
}

/**
  * Returns the list of alien bases.
  * @return Pointer to alien base list.
  */
std::vector<AlienBase*> *SavedGame::getAlienBases()
{
	return &_alienBases;
}

/**
 * Toggles debug mode.
 */
void SavedGame::setDebugMode()
{
	_debug = !_debug;
}

/**
 * Gets the current debug mode.
 * @return Debug mode.
 */
bool SavedGame::getDebugMode() const
{
	return _debug;
}

/** @brief Match a mission based on region and type.
 * This function object will match alien missions based on region and type.
 */
class matchRegionAndType: public std::unary_function<AlienMission *, bool>
{
public:
	/// Store the region and type.
	matchRegionAndType(const std::string &region, const std::string &type) : _region(region), _type(type) { }
	/// Match against stored values.
	bool operator()(const AlienMission *mis) const
	{
		return mis->getRegion() == _region && mis->getType() == _type;
	}
private:

	const std::string &_region;
	const std::string &_type;
};

/**
 * Find a mission from the active alien missions.
 * @param region The region ID.
 * @param type The mission type ID.
 * @return A pointer to the mission, or 0 if no mission matched.
 */
AlienMission *SavedGame::getAlienMission(const std::string &region, const std::string &type) const
{
	std::vector<AlienMission*>::const_iterator ii = std::find_if(_activeMissions.begin(), _activeMissions.end(), matchRegionAndType(region, type));
	if (ii == _activeMissions.end())
		return 0;
	return *ii;
}

/**
 * return the list of monthly maintenance costs
 * @return list of maintenances.
 */
std::vector<int> SavedGame::getMaintenances()
{
	return _maintenance;
}

/**
 * adds to this month's research score
 * @param score the amount to add.
 */
void SavedGame::addResearchScore(int score)
{
	_researchScores.back() += score;
}

/**
 * return the list of research scores
 * @return list of research scores.
 */
std::vector<int> SavedGame::getResearchScores()
{
	return _researchScores;
}

/**
 * return if the player has been 
 * warned about poor performance.
 * @return true or false.
 */
bool SavedGame::getWarned() const
{
	return _warned;
}

/**
 * sets the player's "warned" status.
 * @param warned set "warned" to this.
 */
void SavedGame::setWarned(bool warned)
{
	_warned = warned;
}

/** @brief Check if a point is contained in a region.
 * This function object checks if a point is contained inside a region.
 */
class ContainsPoint: public std::unary_function<const Region *, bool>
{
public:
	/// Remember the coordinates.
	ContainsPoint(double lon, double lat) : _lon(lon), _lat(lat) { /* Empty by design. */ }
	/// Check is the region contains the stored point.
	bool operator()(const Region *region) const { return region->getRules()->insideRegion(_lon, _lat); }
private:
	double _lon, _lat;
};

/**
 * Find the region containing this location.
 * @param lon The longtitude.
 * @param lat The latitude.
 * @return Pointer to the region, or 0.
 */
Region *SavedGame::locateRegion(double lon, double lat) const
{
	std::vector<Region *>::const_iterator found = std::find_if(_regions.begin(), _regions.end(), ContainsPoint(lon, lat));
	if (found != _regions.end())
	{
		return *found;
	}
	return 0;
}

/**
 * Find the region containing this target.
 * @param target The target to locate.
 * @return Pointer to the region, or 0.
 */
Region *SavedGame::locateRegion(const Target &target) const
{
	return locateRegion(target.getLongitude(), target.getLatitude());
}

/*
 * @return the month counter.
 */
int SavedGame::getMonthsPassed() const
{
	return _monthsPassed;
}

/*
 * @return the GraphRegionToggles.
 */
const std::string &SavedGame::getGraphRegionToggles() const
{
	return _graphRegionToggles;
}

/*
 * @return the GraphCountryToggles.
 */
const std::string &SavedGame::getGraphCountryToggles() const
{
	return _graphCountryToggles;
}

/*
 * @return the GraphFinanceToggles.
 */
const std::string &SavedGame::getGraphFinanceToggles() const
{
	return _graphFinanceToggles;
}

/**
 * Sets the GraphRegionToggles.
 * @param value The new value for GraphRegionToggles.
 */
void SavedGame::setGraphRegionToggles(const std::string &value)
{
	_graphRegionToggles = value;
}

/**
 * Sets the GraphCountryToggles.
 * @param value The new value for GraphCountryToggles.
 */
void SavedGame::setGraphCountryToggles(const std::string &value)
{
	_graphCountryToggles = value;
}

/**
 * Sets the GraphFinanceToggles.
 * @param value The new value for GraphFinanceToggles.
 */
void SavedGame::setGraphFinanceToggles(const std::string &value)
{
	_graphFinanceToggles = value;
}

/*
 * Increment the month counter.
 */
void SavedGame::addMonth()
{
	++_monthsPassed;
}

/*
 * Toggles the state of the radar line drawing.
 */
void SavedGame::toggleRadarLines()
{
	_radarLines = !_radarLines;
}

/*
 * @return the state of the radar line drawing.
 */
bool SavedGame::getRadarLines()
{
	return _radarLines;
}

/*
 * Toggles the state of the detail drawing.
 */
void SavedGame::toggleDetail()
{
	_detail = !_detail;
}

/*
 * @return the state of the detail drawing.
 */
bool SavedGame::getDetail()
{
	return _detail;
}

/*
 * marks a research topic as having already come up as "we can now research"
 * @param research is the project we want to add to the vector
 */
void SavedGame::addPoppedResearch(const RuleResearch* research)
{
	if (!wasResearchPopped(research))
		_poppedResearch.push_back(research);
}

/*
 * checks if an unresearched topic has previously been popped up.
 * @param research is the project we are checking for
 * @return whether or not it has been popped up.
 */
bool SavedGame::wasResearchPopped(const RuleResearch* research)
{
	return (std::find(_poppedResearch.begin(), _poppedResearch.end(), research) != _poppedResearch.end());
}

/*
 * checks for and removes a research project from the "has been popped up" array
 * @param research is the project we are checking for and removing, if necessary.
 */
void SavedGame::removePoppedResearch(const RuleResearch* research)
{
	std::vector<const RuleResearch*>::iterator r = std::find(_poppedResearch.begin(), _poppedResearch.end(), research);
	if (r != _poppedResearch.end())
	{
		_poppedResearch.erase(r);
	}
}

}
