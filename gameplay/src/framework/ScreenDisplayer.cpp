#include "framework/Base.h"
#include "framework/ScreenDisplayer.h"

namespace gameplay
{

  ScreenDisplayer* ScreenDisplayer::__scriptInstance = nullptr;

  ScreenDisplayer::ScreenDisplayer() : _time(0L), _startTime(0)
  {
  }

  ScreenDisplayer::~ScreenDisplayer()
  {
    long elapsedTime = (long)(Game::getInstance()->getGameTime() - _startTime);
    if (elapsedTime < _time)
      Platform::sleep(_time - elapsedTime);
  }

  void ScreenDisplayer::start(const char* function, unsigned long time)
  {
    if (__scriptInstance == nullptr)
    {
      __scriptInstance = new ScreenDisplayer();
    }

    __scriptInstance->_time = time;
    Game::getInstance()->renderOnce(function);
    __scriptInstance->_startTime = Game::getInstance()->getGameTime();
  }

  void ScreenDisplayer::finish()
  {
    SAFE_DELETE(__scriptInstance);
  }



}
