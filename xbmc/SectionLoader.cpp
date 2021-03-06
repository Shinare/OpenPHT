/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "threads/SystemClock.h"
#include "system.h"
#include "SectionLoader.h"
#include "cores/DllLoader/DllLoaderContainer.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

using namespace std;

#define g_sectionLoader XBMC_GLOBAL_USE(CSectionLoader)
bool g_sectionLoaderDeinitialized = false;

//  delay for unloading dll's
#define UNLOAD_DELAY 30*1000 // 30 sec.

//Define this to get loggin on all calls to load/unload sections/dlls
//#define LOGALL

CSectionLoader::CSectionLoader(void)
{}

CSectionLoader::~CSectionLoader(void)
{
  g_sectionLoaderDeinitialized = true;
  UnloadAll();
}

LibraryLoader *CSectionLoader::LoadDLL(const CStdString &dllname, bool bDelayUnload /*=true*/, bool bLoadSymbols /*=false*/)
{
  if (g_sectionLoaderDeinitialized) return NULL;
  CSingleLock lock(g_sectionLoader.m_critSection);

  if (!dllname) return NULL;
  // check if it's already loaded, and increase the reference count if so
  for (int i = 0; i < (int)g_sectionLoader.m_vecLoadedDLLs.size(); ++i)
  {
    CDll& dll = g_sectionLoader.m_vecLoadedDLLs[i];
    if (dll.m_strDllName.Equals(dllname))
    {
      dll.m_lReferenceCount++;
      return dll.m_pDll;
    }
  }

  // ok, now load the dll
  CLog::Log(LOGDEBUG, "SECTION:LoadDLL(%s)\n", dllname.c_str());
  LibraryLoader* pDll = DllLoaderContainer::LoadModule(dllname.c_str(), NULL, bLoadSymbols);
  if (!pDll)
    return NULL;

  CDll newDLL;
  newDLL.m_strDllName = dllname;
  newDLL.m_lReferenceCount = 1;
  newDLL.m_bDelayUnload=bDelayUnload;
  newDLL.m_pDll=pDll;
  g_sectionLoader.m_vecLoadedDLLs.push_back(newDLL);

  return newDLL.m_pDll;
}

void CSectionLoader::UnloadDLL(const CStdString &dllname)
{
  if (g_sectionLoaderDeinitialized) return;
  CSingleLock lock(g_sectionLoader.m_critSection);

  if (!dllname) return;
  // check if it's already loaded, and decrease the reference count if so
  for (int i = 0; i < (int)g_sectionLoader.m_vecLoadedDLLs.size(); ++i)
  {
    CDll& dll = g_sectionLoader.m_vecLoadedDLLs[i];
    if (dll.m_strDllName.Equals(dllname))
    {
      dll.m_lReferenceCount--;
      if (0 == dll.m_lReferenceCount)
      {
        if (dll.m_bDelayUnload)
          dll.m_unloadDelayStartTick = XbmcThreads::SystemClockMillis();
        else
        {
          if (dll.m_pDll)
            DllLoaderContainer::ReleaseModule(dll.m_pDll);
          g_sectionLoader.m_vecLoadedDLLs.erase(g_sectionLoader.m_vecLoadedDLLs.begin() + i);
        }
      }
      return;
    }
  }
}

void CSectionLoader::UnloadDelayed()
{
  if (g_sectionLoaderDeinitialized) return;
  CSingleLock lock(g_sectionLoader.m_critSection);

  // check if we can unload any unreferenced dlls
  for (int i = 0; i < (int)g_sectionLoader.m_vecLoadedDLLs.size(); ++i)
  {
    CDll& dll = g_sectionLoader.m_vecLoadedDLLs[i];
    if (dll.m_lReferenceCount == 0 && XbmcThreads::SystemClockMillis() - dll.m_unloadDelayStartTick > UNLOAD_DELAY)
    {
      CLog::Log(LOGDEBUG,"SECTION:UnloadDelayed(DLL: %s)", dll.m_strDllName.c_str());

      if (dll.m_pDll)
        DllLoaderContainer::ReleaseModule(dll.m_pDll);
      g_sectionLoader.m_vecLoadedDLLs.erase(g_sectionLoader.m_vecLoadedDLLs.begin() + i);
      return;
    }
  }
}

void CSectionLoader::UnloadAll()
{
  // delete the dll's
  CSingleLock lock(g_sectionLoader.m_critSection);
  vector<CDll>::iterator it = g_sectionLoader.m_vecLoadedDLLs.begin();
  while (it != g_sectionLoader.m_vecLoadedDLLs.end())
  {
    CDll& dll = *it;
    if (dll.m_pDll)
      DllLoaderContainer::ReleaseModule(dll.m_pDll);
    it = g_sectionLoader.m_vecLoadedDLLs.erase(it);
  }
  g_sectionLoader.m_vecLoadedDLLs.clear();
}
