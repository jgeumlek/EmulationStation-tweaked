#include "views/ViewController.h"
#include "Log.h"
#include "SystemData.h"
#include "Settings.h"

#include "views/gamelist/BasicGameListView.h"
#include "views/gamelist/DetailedGameListView.h"
#include "views/gamelist/GridGameListView.h"
#include "guis/GuiMenu.h"
#include "guis/GuiGamelistOptions.h"
#include "guis/GuiMsgBox.h"
#include "animations/LaunchAnimation.h"
#include "animations/MoveCameraAnimation.h"
#include "animations/LambdaAnimation.h"
#include "SystemManager.h"

ViewController* ViewController::sInstance = NULL;

ViewController* ViewController::get()
{
	assert(sInstance);
	return sInstance;
}

void ViewController::init(Window* window)
{
	assert(!sInstance);
	sInstance = new ViewController(window);
}

ViewController::ViewController(Window* window)
	: GuiComponent(window), mCurrentView(nullptr), mCamera(Eigen::Affine3f::Identity()), mFadeOpacity(0), mLockInput(false)
{
	mState.viewing = NOTHING;
}

ViewController::~ViewController()
{
	assert(sInstance == this);
	sInstance = NULL;
}

void ViewController::goToStart()
{
	// TODO
	/* mState.viewing = START_SCREEN;
	mCurrentView.reset();
	playViewTransition(); */
	goToSystemView(SystemManager::getInstance()->getSystems().at(0));
}

int ViewController::getSystemId(SystemData* system)
{
	const std::vector<SystemData*>& sysVec = SystemManager::getInstance()->getSystems();
	return std::find(sysVec.begin(), sysVec.end(), system) - sysVec.begin();
}

void ViewController::goToSystemView(SystemData* system)
{
	mState.viewing = SYSTEM_SELECT;
	mState.system = system;

	auto systemList = getSystemListView();
	systemList->setPosition(getSystemId(system) * (float)Renderer::getScreenWidth(), systemList->getPosition().y());

	systemList->goToSystem(system, false);
	mCurrentView = systemList;
	mFadeOpacity = 0;
	playViewTransition();
}

void ViewController::goToNextGameList()
{
	assert(mState.viewing == GAME_LIST);
	SystemData* system = getState().getSystem();
	assert(system);
	goToGameList(SystemManager::getInstance()->getNext(system), 1);
}

void ViewController::goToPrevGameList()
{
	assert(mState.viewing == GAME_LIST);
	SystemData* system = getState().getSystem();
	assert(system);
	goToGameList(SystemManager::getInstance()->getPrev(system), -1);
}

void ViewController::goToGameList(SystemData* system, int velocity)
{
	if(mState.viewing == SYSTEM_SELECT)
	{
		// move system list
		auto sysList = getSystemListView();
		float offX = sysList->getPosition().x();
		int sysId = getSystemId(system);
		sysList->setPosition(sysId * (float)Renderer::getScreenWidth(), sysList->getPosition().y());
		offX = sysList->getPosition().x() - offX;
		mCamera.translation().x() -= offX;
		//Recompute meta system entries, so that the system select can be used to refresh them
		if(system->isMetaSystem())
			onFilesChanged(system);
	}

	mState.viewing = GAME_LIST;
	mState.system = system;

	mCurrentView = getGameListView(system);
	//If needed, wrap around before scrolling.
	if(velocity < 0 && mCurrentView->getPosition().x() > -mCamera.translation().x())
	{
		float sceneWidth = (float)Renderer::getScreenWidth() * mGameListViews.size();
		mCamera *= Eigen::Translation3f(-sceneWidth,0,0);
	}
	if(velocity > 0 && mCurrentView->getPosition().x() < -mCamera.translation().x())
	{
		float sceneWidth = (float)Renderer::getScreenWidth() * mGameListViews.size();
		mCamera *= Eigen::Translation3f(sceneWidth,0,0);
	}
	mFadeOpacity = 0;
	playViewTransition();
}

void ViewController::playViewTransition()
{
	Eigen::Vector3f target(Eigen::Vector3f::Identity());
	if(mCurrentView) 
		target = mCurrentView->getPosition();

	// no need to animate, we're not going anywhere (probably goToNextGamelist() or goToPrevGamelist() when there's only 1 system)
	if(target == -mCamera.translation() && !isAnimationPlaying(0))
		return;

	if(Settings::getInstance()->getString("TransitionStyle") == "fade")
	{
		// fade
		// stop whatever's currently playing, leaving mFadeOpacity wherever it is
		cancelAnimation(0);

		auto fadeFunc = [this](float t) {
			mFadeOpacity = lerp<float>(0, 1, t);
		};

		const static int FADE_DURATION = 240; // fade in/out time
		const static int FADE_WAIT = 320; // time to wait between in/out
		setAnimation(new LambdaAnimation(fadeFunc, FADE_DURATION), 0, [this, fadeFunc, target] {
			this->mCamera.translation() = -target;
			updateHelpPrompts();
			setAnimation(new LambdaAnimation(fadeFunc, FADE_DURATION), FADE_WAIT, nullptr, true);
		});

		// fast-forward animation if we're partway faded
		if(target == -mCamera.translation())
		{
			// not changing screens, so cancel the first half entirely
			advanceAnimation(0, FADE_DURATION);
			advanceAnimation(0, FADE_WAIT);
			advanceAnimation(0, FADE_DURATION - (int)(mFadeOpacity * FADE_DURATION));
		}else{
			advanceAnimation(0, (int)(mFadeOpacity * FADE_DURATION));
		}
	}else{
		// slide
		setAnimation(new MoveCameraAnimation(mCamera, target));
		updateHelpPrompts(); // update help prompts immediately
	}
}

void ViewController::onFilesChanged(SystemData* system)
{
	if(system == NULL) // (all systems)
	{
		// onFilesChanged() can cause a view to be recreated, which will invalidate
		// any iterators for mGameListViews, so we make a vector of views to reload
		std::vector<IGameListView*> toReload;
		for(auto it = mGameListViews.begin(); it != mGameListViews.end(); it++)
			toReload.push_back(it->second.get());

		for(auto it = toReload.begin(); it != toReload.end(); it++)
			(*it)->onFilesChanged();

	}else{
		auto it = mGameListViews.find(system);
		if(it != mGameListViews.end())
			it->second->onFilesChanged();
	}
}

void ViewController::onMetaDataChanged(SystemData* system, const FileData& file)
{
	auto it = mGameListViews.find(system);
	if(it != mGameListViews.end())
		it->second->onMetaDataChanged(file);
}
void ViewController::onStatisticsChanged(SystemData* system, const FileData& file)
{
	auto it = mGameListViews.find(system);
	if(it != mGameListViews.end())
		it->second->onStatisticsChanged(file);
}

void ViewController::launch(const FileData& game, Eigen::Vector3f center)
{
	if(game.getType() != GAME)
	{
		LOG(LogError) << "Tried to launch something that isn't a game!";
		return;
	}

	Eigen::Affine3f origCamera = mCamera;
	origCamera.translation() = -mCurrentView->getPosition();

	center += mCurrentView->getPosition();
	stopAnimation(1); // make sure the fade in isn't still playing
	mLockInput = true;

	if(Settings::getInstance()->getString("TransitionStyle") == "fade")
	{
		// fade out, launch game, fade back in
		auto fadeFunc = [this](float t) {
			//t -= 1;
			//mFadeOpacity = lerp<float>(0.0f, 1.0f, t*t*t + 1);
			mFadeOpacity = lerp<float>(0.0f, 1.0f, t);
		};
		setAnimation(new LambdaAnimation(fadeFunc, 800), 0, [this, &game, fadeFunc]
		{
			game.getSystem()->launchGame(mWindow, game);
			mLockInput = false;
			setAnimation(new LambdaAnimation(fadeFunc, 800), 0, nullptr, true);
			this->onStatisticsChanged(game.getSystem(), game);
		});
	}else{
		// move camera to zoom in on center + fade out, launch game, come back in
		setAnimation(new LaunchAnimation(mCamera, mFadeOpacity, center, 1500), 0, [this, origCamera, center, &game] 
		{
			game.getSystem()->launchGame(mWindow, game);
			mCamera = origCamera;
			mLockInput = false;
			setAnimation(new LaunchAnimation(mCamera, mFadeOpacity, center, 600), 0, nullptr, true);
			this->onStatisticsChanged(game.getSystem(), game);
		});
	}
}

std::shared_ptr<IGameListView> ViewController::getGameListView(SystemData* system)
{
	//if we already made one, return that one
	auto exists = mGameListViews.find(system);
	if(exists != mGameListViews.end())
		return exists->second;

	//if we didn't, make it, remember it, and return it
	std::shared_ptr<IGameListView> view;

	//decide type
	if(system->hasFileWithImage())
		view = std::shared_ptr<IGameListView>(new DetailedGameListView(mWindow, system->getRootFolder()));
	else
		view = std::shared_ptr<IGameListView>(new BasicGameListView(mWindow, system->getRootFolder()));
		
	// uncomment for experimental "image grid" view
	//view = std::shared_ptr<IGameListView>(new GridGameListView(mWindow, system->getRootFolder()));

	view->setTheme(system->getTheme());

	const std::vector<SystemData*>& sysVec = SystemManager::getInstance()->getSystems();
	int id = std::find(sysVec.begin(), sysVec.end(), system) - sysVec.begin();
	view->setPosition(id * (float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight() * 2);

	addChild(view.get());

	mGameListViews[system] = view;
	return view;
}

std::shared_ptr<SystemView> ViewController::getSystemListView()
{
	//if we already made one, return that one
	if(mSystemListView)
		return mSystemListView;

	mSystemListView = std::shared_ptr<SystemView>(new SystemView(mWindow));
	addChild(mSystemListView.get());
	mSystemListView->setPosition(0, (float)Renderer::getScreenHeight());
	return mSystemListView;
}

bool ViewController::input(InputConfig* config, Input input)
{
	if(mLockInput)
		return true;

	// open menu
	if(config->isMappedTo("select", input) && input.value != 0)
	{
		// open menu
		if(mState.viewing == GAME_LIST) 
		{
			mWindow->pushGui(new GuiGamelistOptions(mWindow, mState.getSystem()));
		} else {
			mWindow->pushGui(new GuiGamelistOptions(mWindow, nullptr));
		}
		return true;
	}

	if(mCurrentView)
		return mCurrentView->input(config, input);

	return false;
}

void ViewController::update(int deltaTime)
{
	if(mCurrentView)
	{
		mCurrentView->update(deltaTime);
	}

	updateSelf(deltaTime);
}

void ViewController::render(const Eigen::Affine3f& parentTrans)
{
	Eigen::Affine3f trans = mCamera * parentTrans;

	// camera position, position + size
	Eigen::Vector3f viewStart = trans.inverse().translation();
	Eigen::Vector3f viewEnd = trans.inverse() * Eigen::Vector3f((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight(), 0);

	// draw systemview
	getSystemListView()->render(trans);
	Eigen::Affine3f wrapTrans;
	Eigen::Vector3f wrapStart;
	Eigen::Vector3f wrapEnd;
	bool wrapped = false;

	float sceneWidth = (float)Renderer::getScreenWidth() * mGameListViews.size();
	if(viewStart.x() < 0)
	{
		wrapped = true;
		wrapTrans = trans * Eigen::Translation3f(-sceneWidth,0,0);
	}
	if(viewEnd.x() > sceneWidth)
	{
		wrapped = true;
		wrapTrans = trans * Eigen::Translation3f(sceneWidth,0,0);
	}

	if(wrapped)
	{
		wrapStart = wrapTrans.inverse().translation();
		wrapEnd = wrapTrans.inverse() * Eigen::Vector3f((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight(), 0);
	}



	// draw gamelists
	for(auto it = mGameListViews.begin(); it != mGameListViews.end(); it++)
	{
		// clipping
		Eigen::Vector3f guiStart = it->second->getPosition();
		Eigen::Vector3f guiEnd = it->second->getPosition() + Eigen::Vector3f(it->second->getSize().x(), it->second->getSize().y(), 0);

		if(guiEnd.x() >= viewStart.x() && guiEnd.y() >= viewStart.y() &&
			guiStart.x() <= viewEnd.x() && guiStart.y() <= viewEnd.y())
				it->second->render(trans);
		if(wrapped && guiEnd.x() >= wrapStart.x() && guiEnd.y() >= wrapStart.y() &&
			guiStart.x() <= wrapEnd.x() && guiStart.y() <= wrapEnd.y())
				it->second->render(wrapTrans);
	}

	if(mWindow->peekGui() == this)
		mWindow->renderHelpPromptsEarly();

	// fade out
	if(mFadeOpacity)
	{
		Renderer::setMatrix(parentTrans);
		Renderer::drawRect(0, 0, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x00000000 | (unsigned char)(mFadeOpacity * 255));
	}
}

void ViewController::preload()
{
	const std::vector<SystemData*>& systems = SystemManager::getInstance()->getSystems();
	for(auto it = systems.begin(); it != systems.end(); it++)
	{
		getGameListView(*it);
	}
}

void ViewController::reloadGameListView(IGameListView* view, bool reloadTheme)
{
	for(auto it = mGameListViews.begin(); it != mGameListViews.end(); it++)
	{
		if(it->second.get() == view)
		{
			bool isCurrent = (mCurrentView == it->second);
			SystemData* system = it->first;
			FileData cursor = view->getCursor();
			mGameListViews.erase(it);

			if(reloadTheme)
				system->loadTheme();

			std::shared_ptr<IGameListView> newView = getGameListView(system);
			newView->setCursor(cursor);

			if(isCurrent)
				mCurrentView = newView;

			break;
		}
	}
}

void ViewController::reloadAll()
{
	std::map<SystemData*, FileData> cursorMap;
	for(auto it = mGameListViews.begin(); it != mGameListViews.end(); it++)
	{
		cursorMap[it->first] = it->second->getCursor();
	}
	mGameListViews.clear();

	for(auto it = cursorMap.begin(); it != cursorMap.end(); it++)
	{
		it->first->loadTheme();
		getGameListView(it->first)->setCursor(it->second);
	}

	mSystemListView.reset();
	getSystemListView();

	// update mCurrentView since the pointers changed
	if(mState.viewing == GAME_LIST)
	{
		mCurrentView = getGameListView(mState.getSystem());
	}else if(mState.viewing == SYSTEM_SELECT)
	{
		mSystemListView->goToSystem(mState.getSystem(), false);
		mCurrentView = mSystemListView;
	}else{
		goToSystemView(SystemManager::getInstance()->getSystems().front());
	}

	updateHelpPrompts();
}

std::vector<HelpPrompt> ViewController::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if(!mCurrentView)
		return prompts;
	
	prompts = mCurrentView->getHelpPrompts();
	prompts.push_back(HelpPrompt("select", "menu"));

	return prompts;
}

HelpStyle ViewController::getHelpStyle()
{
	if(!mCurrentView)
		return GuiComponent::getHelpStyle();

	return mCurrentView->getHelpStyle();
}
