/*=========================================================================

Program:   Medical Imaging & Interaction Toolkit
Module:    $RCSfile$
Language:  C++
Date:      $Date$
Version:   $Revision$

Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/


#ifndef QMITKRENDERINGMANAGER_H_HEADER_INCLUDED_C135A197
#define QMITKRENDERINGMANAGER_H_HEADER_INCLUDED_C135A197

#include "mitkRenderingManager.h"
#include <qobject.h>

class QTimer;
class QmitkRenderingManagerInternal;
class QmitkRenderingManagerFactory;

/**
 * \brief Qt specific implementation of mitk::RenderingManager.
 *
 * This implementation uses a QTimer object to realize the RenderWindow
 * update timing. The execution of pending updates is controlled by the
 * timer.
 * \ingroup Renderer
 */
class QmitkRenderingManager : public mitk::RenderingManager
{
public:
  mitkClassMacro( QmitkRenderingManager, mitk::RenderingManager );

  virtual ~QmitkRenderingManager();

protected:
  itkFactorylessNewMacro(Self);

  QmitkRenderingManager();

  virtual void RestartTimer();

  virtual void StopTimer();

private:
  QmitkRenderingManagerInternal* m_QmitkRenderingManagerInternal;

  friend class QmitkRenderingManagerFactory;
};

class QmitkRenderingManagerInternal : public QObject
{
  Q_OBJECT

public:  
  friend class QmitkRenderingManager;

  virtual ~QmitkRenderingManagerInternal();

protected:
  QmitkRenderingManagerInternal();

protected slots:

  void RestartTimer();

  void StopTimer();

  void QUpdateCallback();

private:
  QTimer *m_Timer;
  QmitkRenderingManager* m_QmitkRenderingManager;
};

#endif /* MITKRenderingManager_H_HEADER_INCLUDED_C135A197 */
