/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

#include "QmitkSlicesInterpolator.h"

#include "mitkApplyDiffImageOperation.h"
#include "mitkColorProperty.h"
#include "mitkCoreObjectFactory.h"
#include "mitkDiffImageApplier.h"
#include "mitkInteractionConst.h"
#include "mitkLevelWindowProperty.h"
#include "mitkOperationEvent.h"
#include "mitkProgressBar.h"
#include "mitkProperties.h"
#include "mitkRenderingManager.h"
#include "mitkSegTool2D.h"
#include "mitkSliceNavigationController.h"
#include "mitkSurfaceToImageFilter.h"
#include "mitkToolManager.h"
#include "mitkUndoController.h"
#include <mitkExtractSliceFilter.h>
#include <mitkImageReadAccessor.h>
#include <mitkImageTimeSelector.h>
#include <mitkImageWriteAccessor.h>
#include <mitkPlaneProposer.h>
#include <mitkUnstructuredGridClusteringFilter.h>
#include <mitkVtkImageOverwrite.h>
#include <mitkShapeBasedInterpolationAlgorithm.h>

#include <itkCommand.h>

#include <QCheckBox>
#include <QCursor>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <vtkPolyVertex.h>
#include <vtkUnstructuredGrid.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace
{
  template <typename T = mitk::BaseData>
  itk::SmartPointer<T> GetData(const mitk::DataNode* dataNode)
  {
    return nullptr != dataNode
      ? dynamic_cast<T*>(dataNode->GetData())
      : nullptr;
  }
}

float SURFACE_COLOR_RGB[3] = {0.49f, 1.0f, 0.16f};

const std::map<QAction *, mitk::SliceNavigationController *> QmitkSlicesInterpolator::createActionToSliceDimension()
{
  std::map<QAction *, mitk::SliceNavigationController *> actionToSliceDimension;
  foreach (mitk::SliceNavigationController *slicer, m_ControllerToDeleteObserverTag.keys())
  {
    actionToSliceDimension[new QAction(QString::fromStdString(slicer->GetViewDirectionAsString()), nullptr)] = slicer;
  }

  return actionToSliceDimension;
}

QmitkSlicesInterpolator::QmitkSlicesInterpolator(QWidget *parent, const char * /*name*/)
  : QWidget(parent),
    //    ACTION_TO_SLICEDIMENSION( createActionToSliceDimension() ),
    m_Interpolator(mitk::SegmentationInterpolationController::New()),
    m_SurfaceInterpolator(mitk::SurfaceInterpolationController::GetInstance()),
    m_ToolManager(nullptr),
    m_Initialized(false),
    m_LastSNC(nullptr),
    m_LastSliceIndex(0),
    m_2DInterpolationEnabled(false),
    m_3DInterpolationEnabled(false),
    m_FirstRun(true)
{
  m_GroupBoxEnableExclusiveInterpolationMode = new QGroupBox("Interpolation", this);

  QVBoxLayout *vboxLayout = new QVBoxLayout(m_GroupBoxEnableExclusiveInterpolationMode);

  m_EdgeDetector = mitk::FeatureBasedEdgeDetectionFilter::New();
  m_PointScorer = mitk::PointCloudScoringFilter::New();

  m_CmbInterpolation = new QComboBox(m_GroupBoxEnableExclusiveInterpolationMode);
  m_CmbInterpolation->addItem("Disabled");
  m_CmbInterpolation->addItem("2-Dimensional");
  m_CmbInterpolation->addItem("3-Dimensional");
  vboxLayout->addWidget(m_CmbInterpolation);

  m_BtnApply2D = new QPushButton("Confirm for single slice", m_GroupBoxEnableExclusiveInterpolationMode);
  vboxLayout->addWidget(m_BtnApply2D);

  m_BtnApplyForAllSlices2D = new QPushButton("Confirm for all slices", m_GroupBoxEnableExclusiveInterpolationMode);
  vboxLayout->addWidget(m_BtnApplyForAllSlices2D);

  m_BtnApply3D = new QPushButton("Confirm", m_GroupBoxEnableExclusiveInterpolationMode);
  vboxLayout->addWidget(m_BtnApply3D);

  // T28261
  // m_BtnSuggestPlane = new QPushButton("Suggest a plane", m_GroupBoxEnableExclusiveInterpolationMode);
  // vboxLayout->addWidget(m_BtnSuggestPlane);

  m_BtnReinit3DInterpolation = new QPushButton("Reinit Interpolation", m_GroupBoxEnableExclusiveInterpolationMode);
  vboxLayout->addWidget(m_BtnReinit3DInterpolation);

  m_ChkShowPositionNodes = new QCheckBox("Show Position Nodes", m_GroupBoxEnableExclusiveInterpolationMode);
  vboxLayout->addWidget(m_ChkShowPositionNodes);

  this->HideAllInterpolationControls();

  connect(m_CmbInterpolation, SIGNAL(currentIndexChanged(int)), this, SLOT(OnInterpolationMethodChanged(int)));
  connect(m_BtnApply2D, SIGNAL(clicked()), this, SLOT(OnAcceptInterpolationClicked()));
  connect(m_BtnApplyForAllSlices2D, SIGNAL(clicked()), this, SLOT(OnAcceptAllInterpolationsClicked()));
  connect(m_BtnApply3D, SIGNAL(clicked()), this, SLOT(OnAccept3DInterpolationClicked()));

  // T28261
  // connect(m_BtnSuggestPlane, SIGNAL(clicked()), this, SLOT(OnSuggestPlaneClicked()));

  connect(m_BtnReinit3DInterpolation, SIGNAL(clicked()), this, SLOT(OnReinit3DInterpolation()));
  connect(m_ChkShowPositionNodes, SIGNAL(toggled(bool)), this, SLOT(OnShowMarkers(bool)));
  connect(m_ChkShowPositionNodes, SIGNAL(toggled(bool)), this, SIGNAL(SignalShowMarkerNodes(bool)));

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->addWidget(m_GroupBoxEnableExclusiveInterpolationMode);
  this->setLayout(layout);

  itk::ReceptorMemberCommand<QmitkSlicesInterpolator>::Pointer command =
    itk::ReceptorMemberCommand<QmitkSlicesInterpolator>::New();
  command->SetCallbackFunction(this, &QmitkSlicesInterpolator::OnInterpolationInfoChanged);
  InterpolationInfoChangedObserverTag = m_Interpolator->AddObserver(itk::ModifiedEvent(), command);

  itk::ReceptorMemberCommand<QmitkSlicesInterpolator>::Pointer command2 =
    itk::ReceptorMemberCommand<QmitkSlicesInterpolator>::New();
  command2->SetCallbackFunction(this, &QmitkSlicesInterpolator::OnSurfaceInterpolationInfoChanged);
  SurfaceInterpolationInfoChangedObserverTag = m_SurfaceInterpolator->AddObserver(itk::ModifiedEvent(), command2);

  auto command3 = itk::ReceptorMemberCommand<QmitkSlicesInterpolator>::New();
  command3->SetCallbackFunction(this, &QmitkSlicesInterpolator::OnInterpolationAborted);
  InterpolationAbortedObserverTag = m_Interpolator->AddObserver(itk::AbortEvent(), command3);

  // feedback node and its visualization properties
  m_FeedbackNode = mitk::DataNode::New();
  mitk::CoreObjectFactory::GetInstance()->SetDefaultProperties(m_FeedbackNode);

  m_FeedbackNode->SetProperty("binary", mitk::BoolProperty::New(true));
  m_FeedbackNode->SetProperty("outline binary", mitk::BoolProperty::New(true));
  m_FeedbackNode->SetProperty("color", mitk::ColorProperty::New(255.0, 255.0, 0.0));
  m_FeedbackNode->SetProperty("texture interpolation", mitk::BoolProperty::New(false));
  m_FeedbackNode->SetProperty("layer", mitk::IntProperty::New(20));
  m_FeedbackNode->SetProperty("levelwindow", mitk::LevelWindowProperty::New(mitk::LevelWindow(0, 1)));
  m_FeedbackNode->SetProperty("name", mitk::StringProperty::New("Interpolation feedback"));
  m_FeedbackNode->SetProperty("opacity", mitk::FloatProperty::New(0.8));
  m_FeedbackNode->SetProperty("helper object", mitk::BoolProperty::New(true));

  m_InterpolatedSurfaceNode = mitk::DataNode::New();
  m_InterpolatedSurfaceNode->SetProperty("color", mitk::ColorProperty::New(SURFACE_COLOR_RGB));
  m_InterpolatedSurfaceNode->SetProperty("name", mitk::StringProperty::New("Surface Interpolation feedback"));
  m_InterpolatedSurfaceNode->SetProperty("opacity", mitk::FloatProperty::New(0.5));
  m_InterpolatedSurfaceNode->SetProperty("line width", mitk::FloatProperty::New(4.0f));
  m_InterpolatedSurfaceNode->SetProperty("includeInBoundingBox", mitk::BoolProperty::New(false));
  m_InterpolatedSurfaceNode->SetProperty("helper object", mitk::BoolProperty::New(true));
  m_InterpolatedSurfaceNode->SetVisibility(false);

  m_3DContourNode = mitk::DataNode::New();
  m_3DContourNode->SetProperty("color", mitk::ColorProperty::New(0.0, 0.0, 0.0));
  m_3DContourNode->SetProperty("hidden object", mitk::BoolProperty::New(true));
  m_3DContourNode->SetProperty("name", mitk::StringProperty::New("Drawn Contours"));
  m_3DContourNode->SetProperty("material.representation", mitk::VtkRepresentationProperty::New(VTK_WIREFRAME));
  m_3DContourNode->SetProperty("material.wireframeLineWidth", mitk::FloatProperty::New(2.0f));
  m_3DContourNode->SetProperty("3DContourContainer", mitk::BoolProperty::New(true));
  m_3DContourNode->SetProperty("includeInBoundingBox", mitk::BoolProperty::New(false));
  m_3DContourNode->SetVisibility(
    false, mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget0")));
  m_3DContourNode->SetVisibility(
    false, mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget1")));
  m_3DContourNode->SetVisibility(
    false, mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget2")));
  m_3DContourNode->SetVisibility(
    false, mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget3")));

  QWidget::setContentsMargins(0, 0, 0, 0);
  if (QWidget::layout() != nullptr)
  {
    QWidget::layout()->setContentsMargins(0, 0, 0, 0);
  }

  // For running 3D Interpolation in background
  // create a QFuture and a QFutureWatcher

  connect(&m_Watcher, SIGNAL(started()), this, SLOT(StartUpdateInterpolationTimer()));
  connect(&m_Watcher, SIGNAL(finished()), this, SLOT(OnSurfaceInterpolationFinished()));
  connect(&m_Watcher, SIGNAL(finished()), this, SLOT(StopUpdateInterpolationTimer()));
  m_Timer = new QTimer(this);
  connect(m_Timer, SIGNAL(timeout()), this, SLOT(ChangeSurfaceColor()));
}

void QmitkSlicesInterpolator::SetDataStorage(mitk::DataStorage::Pointer storage)
{
  if (m_DataStorage == storage)
  {
    return;
  }

  if (m_DataStorage.IsNotNull())
  {
    m_DataStorage->RemoveNodeEvent.RemoveListener(
      mitk::MessageDelegate1<QmitkSlicesInterpolator, const mitk::DataNode*>(this, &QmitkSlicesInterpolator::NodeRemoved)
    );
  }

  m_DataStorage = storage;
  m_SurfaceInterpolator->SetDataStorage(storage);

  if (m_DataStorage.IsNotNull())
  {
    m_DataStorage->RemoveNodeEvent.AddListener(
      mitk::MessageDelegate1<QmitkSlicesInterpolator, const mitk::DataNode*>(this, &QmitkSlicesInterpolator::NodeRemoved)
    );
  }
}

mitk::DataStorage *QmitkSlicesInterpolator::GetDataStorage()
{
  if (m_DataStorage.IsNotNull())
  {
    return m_DataStorage;
  }
  else
  {
    return nullptr;
  }
}

void QmitkSlicesInterpolator::Initialize(mitk::ToolManager *toolManager,
                                         const QList<mitk::SliceNavigationController *> &controllers)
{
  Q_ASSERT(!controllers.empty());

  if (m_Initialized)
  {
    // remove old observers
    Uninitialize();
  }

  m_ToolManager = toolManager;

  if (m_ToolManager)
  {
    // set enabled only if a segmentation is selected
    mitk::DataNode *node = m_ToolManager->GetWorkingData(0);
    QWidget::setEnabled(node != nullptr);

    // react whenever the set of selected segmentation changes
    m_ToolManager->WorkingDataChanged +=
      mitk::MessageDelegate<QmitkSlicesInterpolator>(this, &QmitkSlicesInterpolator::OnToolManagerWorkingDataModified);
    m_ToolManager->ReferenceDataChanged += mitk::MessageDelegate<QmitkSlicesInterpolator>(
      this, &QmitkSlicesInterpolator::OnToolManagerReferenceDataModified);

    // connect to the slice navigation controller. after each change, call the interpolator
    foreach (mitk::SliceNavigationController *slicer, controllers)
    {
      // Has to be initialized
      m_LastSNC = slicer;
      m_TimePoints.insert(slicer, slicer->GetSelectedTimePoint());

      itk::MemberCommand<QmitkSlicesInterpolator>::Pointer deleteCommand =
        itk::MemberCommand<QmitkSlicesInterpolator>::New();
      deleteCommand->SetCallbackFunction(this, &QmitkSlicesInterpolator::OnSliceNavigationControllerDeleted);
      m_ControllerToDeleteObserverTag.insert(slicer, slicer->AddObserver(itk::DeleteEvent(), deleteCommand));

      itk::MemberCommand<QmitkSlicesInterpolator>::Pointer timeChangedCommand =
        itk::MemberCommand<QmitkSlicesInterpolator>::New();
      timeChangedCommand->SetCallbackFunction(this, &QmitkSlicesInterpolator::OnTimeChanged);
      m_ControllerToTimeObserverTag.insert(
        slicer, slicer->AddObserver(mitk::SliceNavigationController::TimeGeometryEvent(nullptr, 0), timeChangedCommand));

      itk::MemberCommand<QmitkSlicesInterpolator>::Pointer sliceChangedCommand =
        itk::MemberCommand<QmitkSlicesInterpolator>::New();
      sliceChangedCommand->SetCallbackFunction(this, &QmitkSlicesInterpolator::OnSliceChanged);
      m_ControllerToSliceObserverTag.insert(
        slicer, slicer->AddObserver(mitk::SliceNavigationController::GeometrySliceEvent(nullptr, 0), sliceChangedCommand));
    }
    ACTION_TO_SLICEDIMENSION = createActionToSliceDimension();
  }

  m_Initialized = true;
}

void QmitkSlicesInterpolator::Uninitialize()
{
  if (m_ToolManager.IsNotNull())
  {
    m_ToolManager->WorkingDataChanged -=
      mitk::MessageDelegate<QmitkSlicesInterpolator>(this, &QmitkSlicesInterpolator::OnToolManagerWorkingDataModified);
    m_ToolManager->ReferenceDataChanged -= mitk::MessageDelegate<QmitkSlicesInterpolator>(
      this, &QmitkSlicesInterpolator::OnToolManagerReferenceDataModified);
  }

  foreach (mitk::SliceNavigationController *slicer, m_ControllerToSliceObserverTag.keys())
  {
    slicer->RemoveObserver(m_ControllerToDeleteObserverTag.take(slicer));
    slicer->RemoveObserver(m_ControllerToTimeObserverTag.take(slicer));
    slicer->RemoveObserver(m_ControllerToSliceObserverTag.take(slicer));
  }

  ACTION_TO_SLICEDIMENSION.clear();

  m_ToolManager = nullptr;

  m_Initialized = false;
}

QmitkSlicesInterpolator::~QmitkSlicesInterpolator()
{
  if (m_Initialized)
  {
    // remove old observers
    Uninitialize();
  }

  WaitForFutures();

  if (m_DataStorage.IsNotNull())
  {
    m_DataStorage->RemoveNodeEvent.RemoveListener(
      mitk::MessageDelegate1<QmitkSlicesInterpolator, const mitk::DataNode*>(this, &QmitkSlicesInterpolator::NodeRemoved)
    );
    if (m_DataStorage->Exists(m_3DContourNode))
      m_DataStorage->Remove(m_3DContourNode);
    if (m_DataStorage->Exists(m_InterpolatedSurfaceNode))
      m_DataStorage->Remove(m_InterpolatedSurfaceNode);
  }

  // remove observer
  m_Interpolator->RemoveObserver(InterpolationAbortedObserverTag);
  m_Interpolator->RemoveObserver(InterpolationInfoChangedObserverTag);
  m_SurfaceInterpolator->RemoveObserver(SurfaceInterpolationInfoChangedObserverTag);

  delete m_Timer;
}

/**
External enableization...
*/
void QmitkSlicesInterpolator::setEnabled(bool enable)
{
  QWidget::setEnabled(enable);

  // Set the gui elements of the different interpolation modi enabled
  if (enable)
  {
    if (m_2DInterpolationEnabled)
    {
      this->Show2DInterpolationControls(true);
      m_Interpolator->Activate2DInterpolation(true);
    }
    else if (m_3DInterpolationEnabled)
    {
      this->Show3DInterpolationControls(true);
      this->Show3DInterpolationResult(true);
    }
  }
  // Set all gui elements of the interpolation disabled
  else
  {
    this->HideAllInterpolationControls();
    this->Show3DInterpolationResult(false);
  }
}

void QmitkSlicesInterpolator::On2DInterpolationEnabled(bool status)
{
  OnInterpolationActivated(status);
  m_Interpolator->Activate2DInterpolation(status);
}

void QmitkSlicesInterpolator::On3DInterpolationEnabled(bool status)
{
  On3DInterpolationActivated(status);
}

void QmitkSlicesInterpolator::OnInterpolationDisabled(bool status)
{
  if (status)
  {
    OnInterpolationActivated(!status);
    On3DInterpolationActivated(!status);
    this->Show3DInterpolationResult(false);
  }
}

void QmitkSlicesInterpolator::HideAllInterpolationControls()
{
  this->Show2DInterpolationControls(false);
  this->Show3DInterpolationControls(false);
}

void QmitkSlicesInterpolator::Show2DInterpolationControls(bool show)
{
  m_BtnApply2D->setVisible(show);
  m_BtnApplyForAllSlices2D->setVisible(show);
}

void QmitkSlicesInterpolator::Show3DInterpolationControls(bool show)
{
  m_BtnApply3D->setVisible(show);

  // T28261
  // m_BtnSuggestPlane->setVisible(show);

  m_ChkShowPositionNodes->setVisible(show);
  m_BtnReinit3DInterpolation->setVisible(show);
}

void QmitkSlicesInterpolator::OnInterpolationMethodChanged(int index)
{
  switch (index)
  {
    case 0: // Disabled
      m_GroupBoxEnableExclusiveInterpolationMode->setTitle("Interpolation");
      this->HideAllInterpolationControls();
      this->OnInterpolationActivated(false);
      this->On3DInterpolationActivated(false);
      this->Show3DInterpolationResult(false);
      m_Interpolator->Activate2DInterpolation(false);
      break;

    case 1: // 2D
      m_GroupBoxEnableExclusiveInterpolationMode->setTitle("Interpolation (Enabled)");
      this->HideAllInterpolationControls();
      this->Show2DInterpolationControls(true);
      this->OnInterpolationActivated(true);
      this->On3DInterpolationActivated(false);
      m_Interpolator->Activate2DInterpolation(true);
      break;

    case 2: // 3D
      m_GroupBoxEnableExclusiveInterpolationMode->setTitle("Interpolation (Enabled)");
      this->HideAllInterpolationControls();
      this->Show3DInterpolationControls(true);
      this->OnInterpolationActivated(false);
      this->On3DInterpolationActivated(true);
      m_Interpolator->Activate2DInterpolation(false);
      break;

    default:
      MITK_ERROR << "Unknown interpolation method!";
      m_CmbInterpolation->setCurrentIndex(0);
      break;
  }
}

void QmitkSlicesInterpolator::OnShowMarkers(bool state)
{
  mitk::DataStorage::SetOfObjects::ConstPointer allContourMarkers =
    m_DataStorage->GetSubset(mitk::NodePredicateProperty::New("isContourMarker", mitk::BoolProperty::New(true)));

  for (mitk::DataStorage::SetOfObjects::ConstIterator it = allContourMarkers->Begin(); it != allContourMarkers->End();
       ++it)
  {
    it->Value()->SetProperty("helper object", mitk::BoolProperty::New(!state));
  }
}

void QmitkSlicesInterpolator::OnToolManagerWorkingDataModified()
{
  if (m_ToolManager->GetWorkingData(0) != nullptr)
  {
    m_Segmentation = dynamic_cast<mitk::Image *>(m_ToolManager->GetWorkingData(0)->GetData());
    m_BtnReinit3DInterpolation->setEnabled(true);
  }
  else
  {
    // If no workingdata is set, remove the interpolation feedback
    this->GetDataStorage()->Remove(m_FeedbackNode);
    m_FeedbackNode->SetData(nullptr);
    this->GetDataStorage()->Remove(m_3DContourNode);
    m_3DContourNode->SetData(nullptr);
    this->GetDataStorage()->Remove(m_InterpolatedSurfaceNode);
    m_InterpolatedSurfaceNode->SetData(nullptr);
    m_BtnReinit3DInterpolation->setEnabled(false);
    return;
  }
  // Updating the current selected segmentation for the 3D interpolation
  SetCurrentContourListID();

  if (m_2DInterpolationEnabled)
  {
    OnInterpolationActivated(true); // re-initialize if needed
  }
  this->CheckSupportedImageDimension();
}

void QmitkSlicesInterpolator::OnToolManagerReferenceDataModified()
{
}

void QmitkSlicesInterpolator::OnTimeChanged(itk::Object *sender, const itk::EventObject &e)
{
  // Check if we really have a GeometryTimeEvent
  if (!dynamic_cast<const mitk::SliceNavigationController::GeometryTimeEvent *>(&e))
    return;

  mitk::SliceNavigationController *slicer = dynamic_cast<mitk::SliceNavigationController *>(sender);
  Q_ASSERT(slicer);

  const auto timePoint = slicer->GetSelectedTimePoint();
  m_TimePoints[slicer] = timePoint;

  m_SurfaceInterpolator->SetCurrentTimePoint(timePoint);

  if (m_LastSNC == slicer)
  {
    slicer->SendSlice(); // will trigger a new interpolation
  }
}

void QmitkSlicesInterpolator::OnSliceChanged(itk::Object *sender, const itk::EventObject &e)
{
  // Check whether we really have a GeometrySliceEvent
  if (!dynamic_cast<const mitk::SliceNavigationController::GeometrySliceEvent *>(&e))
    return;

  mitk::SliceNavigationController *slicer = dynamic_cast<mitk::SliceNavigationController *>(sender);

  if (TranslateAndInterpolateChangedSlice(e, slicer))
  {
    slicer->GetRenderer()->RequestUpdate();
  }
}

bool QmitkSlicesInterpolator::TranslateAndInterpolateChangedSlice(const itk::EventObject &e,
                                                                  mitk::SliceNavigationController *slicer)
{
  if (!m_2DInterpolationEnabled)
    return false;

  try
  {
    const mitk::SliceNavigationController::GeometrySliceEvent &event =
      dynamic_cast<const mitk::SliceNavigationController::GeometrySliceEvent &>(e);

    mitk::TimeGeometry *tsg = event.GetTimeGeometry();
    if (tsg && m_TimePoints.contains(slicer) && tsg->IsValidTimePoint(m_TimePoints[slicer]))
    {
      mitk::SlicedGeometry3D *slicedGeometry =
        dynamic_cast<mitk::SlicedGeometry3D *>(tsg->GetGeometryForTimePoint(m_TimePoints[slicer]).GetPointer());
      if (slicedGeometry)
      {
        m_LastSNC = slicer;
        mitk::PlaneGeometry *plane =
          dynamic_cast<mitk::PlaneGeometry *>(slicedGeometry->GetPlaneGeometry(event.GetPos()));
        if (plane)
          Interpolate(plane, m_TimePoints[slicer], slicer);
        return true;
      }
    }
  }
  catch (const std::bad_cast &)
  {
    return false; // so what
  }

  return false;
}

void QmitkSlicesInterpolator::Interpolate(mitk::PlaneGeometry *plane,
                                          mitk::TimePointType timePoint,
                                          mitk::SliceNavigationController *slicer)
{
  if (m_ToolManager)
  {
    mitk::DataNode *node = m_ToolManager->GetWorkingData(0);
    if (node)
    {
      m_Segmentation = dynamic_cast<mitk::Image *>(node->GetData());
      if (m_Segmentation)
      {
        if (!m_Segmentation->GetTimeGeometry()->IsValidTimePoint(timePoint))
        {
          MITK_WARN << "Cannot interpolate segmentation. Passed time point is not within the time bounds of WorkingImage. Time point: " << timePoint;
          return;
        }
        const auto timeStep = m_Segmentation->GetTimeGeometry()->TimePointToTimeStep(timePoint);

        int clickedSliceDimension(-1);
        int clickedSliceIndex(-1);

        // calculate real slice position, i.e. slice of the image
        mitk::SegTool2D::DetermineAffectedImageSlice(m_Segmentation, plane, clickedSliceDimension, clickedSliceIndex);

        mitk::Image::Pointer interpolation =
          m_Interpolator->Interpolate(clickedSliceDimension, clickedSliceIndex, plane, timeStep);
        m_FeedbackNode->SetData(interpolation);

        m_LastSNC = slicer;
        m_LastSliceIndex = clickedSliceIndex;
      }
    }
  }
}

void QmitkSlicesInterpolator::OnSurfaceInterpolationFinished()
{
  mitk::Surface::Pointer interpolatedSurface = m_SurfaceInterpolator->GetInterpolationResult();
  mitk::DataNode *workingNode = m_ToolManager->GetWorkingData(0);

  if (interpolatedSurface.IsNotNull() && workingNode &&
      workingNode->IsVisible(
        mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget2"))))
  {
    m_BtnApply3D->setEnabled(true);

    // T28261
    // m_BtnSuggestPlane->setEnabled(true);

    m_InterpolatedSurfaceNode->SetData(interpolatedSurface);
    m_3DContourNode->SetData(m_SurfaceInterpolator->GetContoursAsSurface());

    this->Show3DInterpolationResult(true);

    if (!m_DataStorage->Exists(m_InterpolatedSurfaceNode))
    {
      m_DataStorage->Add(m_InterpolatedSurfaceNode);
    }
    if (!m_DataStorage->Exists(m_3DContourNode))
    {
      m_DataStorage->Add(m_3DContourNode, workingNode);
    }
  }
  else if (interpolatedSurface.IsNull())
  {
    m_BtnApply3D->setEnabled(false);

    // T28261
    // m_BtnSuggestPlane->setEnabled(false);

    if (m_DataStorage->Exists(m_InterpolatedSurfaceNode))
    {
      this->Show3DInterpolationResult(false);
    }
  }

  m_BtnReinit3DInterpolation->setEnabled(true);

  foreach (mitk::SliceNavigationController *slicer, m_ControllerToTimeObserverTag.keys())
  {
    slicer->GetRenderer()->RequestUpdate();
  }
}

void QmitkSlicesInterpolator::OnAcceptInterpolationClicked()
{
  if (m_Segmentation && m_FeedbackNode->GetData())
  {
    // Make sure that for reslicing and overwriting the same alogrithm is used. We can specify the mode of the vtk
    // reslicer
    vtkSmartPointer<mitkVtkImageOverwrite> reslice = vtkSmartPointer<mitkVtkImageOverwrite>::New();

    // Set slice as input
    mitk::Image::Pointer slice = dynamic_cast<mitk::Image *>(m_FeedbackNode->GetData());
    reslice->SetInputSlice(slice->GetSliceData()->GetVtkImageAccessor(slice)->GetVtkImageData());
    // set overwrite mode to true to write back to the image volume
    reslice->SetOverwriteMode(true);
    reslice->Modified();

    const auto timePoint = m_LastSNC->GetSelectedTimePoint();
    if (!m_Segmentation->GetTimeGeometry()->IsValidTimePoint(timePoint))
    {
      MITK_WARN << "Cannot accept interpolation. Time point selected by SliceNavigationController is not within the time bounds of segmentation. Time point: " << timePoint;
      return;
    }

    mitk::ExtractSliceFilter::Pointer extractor = mitk::ExtractSliceFilter::New(reslice);
    extractor->SetInput(m_Segmentation);
    const auto timeStep = m_Segmentation->GetTimeGeometry()->TimePointToTimeStep(timePoint);
    extractor->SetTimeStep(timeStep);
    extractor->SetWorldGeometry(m_LastSNC->GetCurrentPlaneGeometry());
    extractor->SetVtkOutputRequest(true);
    extractor->SetResliceTransformByGeometry(m_Segmentation->GetTimeGeometry()->GetGeometryForTimeStep(timeStep));

    extractor->Modified();
    extractor->Update();

    // the image was modified within the pipeline, but not marked so
    m_Segmentation->Modified();
    m_Segmentation->GetVtkImageData()->Modified();

    m_FeedbackNode->SetData(nullptr);
    mitk::RenderingManager::GetInstance()->RequestUpdateAll();
  }
}

void QmitkSlicesInterpolator::AcceptAllInterpolations(mitk::SliceNavigationController *slicer)
{
  /*
   * What exactly is done here:
   * 1. We create an empty diff image for the current segmentation
   * 2. All interpolated slices are written into the diff image
   * 3. Then the diffimage is applied to the original segmentation
   */
  if (m_Segmentation)
  {
    mitk::Image::Pointer segmentation3D = m_Segmentation;
    unsigned int timeStep = 0;
    const auto timePoint = slicer->GetSelectedTimePoint();

    if (4 == m_Segmentation->GetDimension())
    {
      const auto* geometry = m_Segmentation->GetTimeGeometry();

      if (!geometry->IsValidTimePoint(timePoint))
      {
        MITK_WARN << "Cannot accept all interpolations. Time point selected by passed SliceNavigationController is not within the time bounds of segmentation. Time point: " << timePoint;
        return;
      }

      timeStep = geometry->TimePointToTimeStep(timePoint);

      auto timeSelector = mitk::ImageTimeSelector::New();
      timeSelector->SetInput(m_Segmentation);
      timeSelector->SetTimeNr(timeStep);
      timeSelector->Update();

      segmentation3D = timeSelector->GetOutput();
    }

    // Create an empty diff image for the undo operation
    auto diffImage = mitk::Image::New();
    diffImage->Initialize(segmentation3D);

    // Create scope for ImageWriteAccessor so that the accessor is destroyed right after use
    {
      mitk::ImageWriteAccessor accessor(diffImage);

      // Set all pixels to zero
      auto pixelType = mitk::MakeScalarPixelType<mitk::Tool::DefaultSegmentationDataType>();

      // For legacy purpose support former pixel type of segmentations (before multilabel)
      if (itk::IOComponentEnum::UCHAR == m_Segmentation->GetImageDescriptor()->GetChannelDescriptor().GetPixelType().GetComponentType())
        pixelType = mitk::MakeScalarPixelType<unsigned char>();

      memset(accessor.GetData(), 0, pixelType.GetSize() * diffImage->GetDimension(0) * diffImage->GetDimension(1) * diffImage->GetDimension(2));
    }

    // Since we need to shift the plane it must be clone so that the original plane isn't altered
    auto slicedGeometry = m_Segmentation->GetSlicedGeometry();
    auto planeGeometry = slicer->GetCurrentPlaneGeometry()->Clone();
    int sliceDimension = -1;
    int sliceIndex = -1;

    mitk::SegTool2D::DetermineAffectedImageSlice(m_Segmentation, planeGeometry, sliceDimension, sliceIndex);

    const auto numSlices = m_Segmentation->GetDimension(sliceDimension);
    mitk::ProgressBar::GetInstance()->AddStepsToDo(numSlices);

    std::atomic_uint totalChangedSlices;

    // Reuse interpolation algorithm instance for each slice to cache boundary calculations
    auto algorithm = mitk::ShapeBasedInterpolationAlgorithm::New();

    // Distribute slice interpolations to multiple threads
    const auto numThreads = std::min(std::thread::hardware_concurrency(), numSlices);
    std::vector<std::vector<unsigned int>> sliceIndices(numThreads);

    for (std::remove_const_t<decltype(numSlices)> sliceIndex = 0; sliceIndex < numSlices; ++sliceIndex)
      sliceIndices[sliceIndex % numThreads].push_back(sliceIndex);

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    // This lambda will be executed by the threads
    auto interpolate = [=, &interpolator = m_Interpolator, &totalChangedSlices](unsigned int threadIndex)
    {
      auto clonedPlaneGeometry = planeGeometry->Clone();
      auto origin = clonedPlaneGeometry->GetOrigin();

      for (auto sliceIndex : sliceIndices[threadIndex])
      {
        slicedGeometry->WorldToIndex(origin, origin);
        origin[sliceDimension] = sliceIndex;
        slicedGeometry->IndexToWorld(origin, origin);
        clonedPlaneGeometry->SetOrigin(origin);

        auto interpolation = interpolator->Interpolate(sliceDimension, sliceIndex, clonedPlaneGeometry, timeStep, algorithm);

        if (interpolation.IsNotNull())
        {
          // Setting up the reslicing pipeline which allows us to write the interpolation results back into the image volume
          auto reslicer = vtkSmartPointer<mitkVtkImageOverwrite>::New();

          // Set overwrite mode to true to write back to the image volume
          reslicer->SetInputSlice(interpolation->GetSliceData()->GetVtkImageAccessor(interpolation)->GetVtkImageData());
          reslicer->SetOverwriteMode(true);
          reslicer->Modified();

          auto diffSliceWriter = mitk::ExtractSliceFilter::New(reslicer);

          diffSliceWriter->SetInput(diffImage);
          diffSliceWriter->SetTimeStep(0);
          diffSliceWriter->SetWorldGeometry(clonedPlaneGeometry);
          diffSliceWriter->SetVtkOutputRequest(true);
          diffSliceWriter->SetResliceTransformByGeometry(diffImage->GetTimeGeometry()->GetGeometryForTimeStep(0));
          diffSliceWriter->Modified();
          diffSliceWriter->Update();

          ++totalChangedSlices;
        }

        mitk::ProgressBar::GetInstance()->Progress();
      }
    };

    m_Interpolator->EnableSliceImageCache();

    for (std::remove_const_t<decltype(numThreads)> threadIndex = 0; threadIndex < numThreads; ++threadIndex)
      threads.emplace_back(interpolate, threadIndex); // Run the interpolation

    for (auto& thread : threads)
      thread.join();

    m_Interpolator->DisableSliceImageCache();

    if (totalChangedSlices > 0)
    {
      // Create do/undo operations
      auto* doOp = new mitk::ApplyDiffImageOperation(mitk::OpTEST, m_Segmentation, diffImage, timeStep);

      auto* undoOp = new mitk::ApplyDiffImageOperation(mitk::OpTEST, m_Segmentation, diffImage, timeStep);
      undoOp->SetFactor(-1.0);

      auto comment = "Confirm all interpolations (" + std::to_string(totalChangedSlices) + ")";

      auto* undoStackItem = new mitk::OperationEvent(mitk::DiffImageApplier::GetInstanceForUndo(), doOp, undoOp, comment);

      mitk::OperationEvent::IncCurrGroupEventId();
      mitk::OperationEvent::IncCurrObjectEventId();
      mitk::UndoController::GetCurrentUndoModel()->SetOperationEvent(undoStackItem);

      // Apply the changes to the original image
      mitk::DiffImageApplier::GetInstanceForUndo()->ExecuteOperation(doOp);
    }

    m_FeedbackNode->SetData(nullptr);
  }

  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}

void QmitkSlicesInterpolator::FinishInterpolation(mitk::SliceNavigationController *slicer)
{
  // this redirect is for calling from outside

  if (slicer == nullptr)
    OnAcceptAllInterpolationsClicked();
  else
    AcceptAllInterpolations(slicer);
}

void QmitkSlicesInterpolator::OnAcceptAllInterpolationsClicked()
{
  QMenu orientationPopup(this);
  std::map<QAction *, mitk::SliceNavigationController *>::const_iterator it;
  for (it = ACTION_TO_SLICEDIMENSION.begin(); it != ACTION_TO_SLICEDIMENSION.end(); it++)
    orientationPopup.addAction(it->first);

  connect(&orientationPopup, SIGNAL(triggered(QAction *)), this, SLOT(OnAcceptAllPopupActivated(QAction *)));

  orientationPopup.exec(QCursor::pos());
}

void QmitkSlicesInterpolator::OnAccept3DInterpolationClicked()
{
  auto referenceImage = GetData<mitk::Image>(m_ToolManager->GetReferenceData(0));

  auto* segmentationDataNode = m_ToolManager->GetWorkingData(0);
  auto segmentation = GetData<mitk::Image>(segmentationDataNode);

  if (referenceImage.IsNull() || segmentation.IsNull())
    return;

  const auto* segmentationGeometry = segmentation->GetTimeGeometry();
  const auto timePoint = m_LastSNC->GetSelectedTimePoint();

  if (!referenceImage->GetTimeGeometry()->IsValidTimePoint(timePoint) ||
      !segmentationGeometry->IsValidTimePoint(timePoint))
  {
    MITK_WARN << "Cannot accept interpolation. Current time point is not within the time bounds of the patient image and segmentation.";
    return;
  }

  auto interpolatedSurface = GetData<mitk::Surface>(m_InterpolatedSurfaceNode);

  if (interpolatedSurface.IsNull())
    return;

  auto surfaceToImageFilter = mitk::SurfaceToImageFilter::New();

  surfaceToImageFilter->SetImage(referenceImage);
  surfaceToImageFilter->SetMakeOutputBinary(true);
  surfaceToImageFilter->SetUShortBinaryPixelType(itk::IOComponentEnum::USHORT == segmentation->GetPixelType().GetComponentType());
  surfaceToImageFilter->SetInput(interpolatedSurface);
  surfaceToImageFilter->Update();

  mitk::Image::Pointer interpolatedSegmentation = surfaceToImageFilter->GetOutput();

  auto timeStep = interpolatedSegmentation->GetTimeGeometry()->TimePointToTimeStep(timePoint);
  mitk::ImageReadAccessor readAccessor(interpolatedSegmentation, interpolatedSegmentation->GetVolumeData(timeStep));
  const auto* dataPointer = readAccessor.GetData();

  if (nullptr == dataPointer)
    return;

  timeStep = segmentationGeometry->TimePointToTimeStep(timePoint);
  segmentation->SetVolume(dataPointer, timeStep, 0);

  m_CmbInterpolation->setCurrentIndex(0);
  this->Show3DInterpolationResult(false);

  std::string name = segmentationDataNode->GetName() + "_3D-interpolation";
  mitk::TimeBounds timeBounds;

  if (1 < interpolatedSurface->GetTimeSteps())
  {
    name += "_t" + std::to_string(timeStep);

    auto* polyData = vtkPolyData::New();
    polyData->DeepCopy(interpolatedSurface->GetVtkPolyData(timeStep));

    auto surface = mitk::Surface::New();
    surface->SetVtkPolyData(polyData);

    interpolatedSurface = surface;
    timeBounds = segmentationGeometry->GetTimeBounds(timeStep);
  }
  else
  {
    timeBounds = segmentationGeometry->GetTimeBounds(0);
  }

  auto* surfaceGeometry = static_cast<mitk::ProportionalTimeGeometry*>(interpolatedSurface->GetTimeGeometry());
  surfaceGeometry->SetFirstTimePoint(timeBounds[0]);
  surfaceGeometry->SetStepDuration(timeBounds[1] - timeBounds[0]);

  // Typical file formats for surfaces do not save any time-related information. As a workaround at least for MITK scene files, we have the
  // possibility to seralize this information as properties.

  interpolatedSurface->SetProperty("ProportionalTimeGeometry.FirstTimePoint", mitk::FloatProperty::New(surfaceGeometry->GetFirstTimePoint()));
  interpolatedSurface->SetProperty("ProportionalTimeGeometry.StepDuration", mitk::FloatProperty::New(surfaceGeometry->GetStepDuration()));

  auto interpolatedSurfaceDataNode = mitk::DataNode::New();

  interpolatedSurfaceDataNode->SetData(interpolatedSurface);
  interpolatedSurfaceDataNode->SetName(name);
  interpolatedSurfaceDataNode->SetOpacity(0.7f);

  std::array<float, 3> rgb;
  segmentationDataNode->GetColor(rgb.data());
  interpolatedSurfaceDataNode->SetColor(rgb.data());

  m_DataStorage->Add(interpolatedSurfaceDataNode, segmentationDataNode);
}

void ::QmitkSlicesInterpolator::OnSuggestPlaneClicked()
{
  if (m_PlaneWatcher.isRunning())
    m_PlaneWatcher.waitForFinished();
  m_PlaneFuture = QtConcurrent::run(this, &QmitkSlicesInterpolator::RunPlaneSuggestion);
  m_PlaneWatcher.setFuture(m_PlaneFuture);
}

void ::QmitkSlicesInterpolator::RunPlaneSuggestion()
{
  if (m_FirstRun)
    mitk::ProgressBar::GetInstance()->AddStepsToDo(7);
  else
    mitk::ProgressBar::GetInstance()->AddStepsToDo(3);

  m_EdgeDetector->SetSegmentationMask(m_Segmentation);
  m_EdgeDetector->SetInput(dynamic_cast<mitk::Image *>(m_ToolManager->GetReferenceData(0)->GetData()));
  m_EdgeDetector->Update();

  mitk::UnstructuredGrid::Pointer uGrid = mitk::UnstructuredGrid::New();
  uGrid->SetVtkUnstructuredGrid(m_EdgeDetector->GetOutput()->GetVtkUnstructuredGrid());

  mitk::ProgressBar::GetInstance()->Progress();

  mitk::Surface::Pointer surface = dynamic_cast<mitk::Surface *>(m_InterpolatedSurfaceNode->GetData());

  vtkSmartPointer<vtkPolyData> vtkpoly = surface->GetVtkPolyData();
  vtkSmartPointer<vtkPoints> vtkpoints = vtkpoly->GetPoints();

  vtkSmartPointer<vtkUnstructuredGrid> vGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
  vtkSmartPointer<vtkPolyVertex> verts = vtkSmartPointer<vtkPolyVertex>::New();

  verts->GetPointIds()->SetNumberOfIds(vtkpoints->GetNumberOfPoints());
  for (int i = 0; i < vtkpoints->GetNumberOfPoints(); i++)
  {
    verts->GetPointIds()->SetId(i, i);
  }

  vGrid->Allocate(1);
  vGrid->InsertNextCell(verts->GetCellType(), verts->GetPointIds());
  vGrid->SetPoints(vtkpoints);

  mitk::UnstructuredGrid::Pointer interpolationGrid = mitk::UnstructuredGrid::New();
  interpolationGrid->SetVtkUnstructuredGrid(vGrid);

  m_PointScorer->SetInput(0, uGrid);
  m_PointScorer->SetInput(1, interpolationGrid);
  m_PointScorer->Update();

  mitk::UnstructuredGrid::Pointer scoredGrid = mitk::UnstructuredGrid::New();
  scoredGrid = m_PointScorer->GetOutput();

  mitk::ProgressBar::GetInstance()->Progress();

  double spacing = mitk::SurfaceInterpolationController::GetInstance()->GetDistanceImageSpacing();
  mitk::UnstructuredGridClusteringFilter::Pointer clusterFilter = mitk::UnstructuredGridClusteringFilter::New();
  clusterFilter->SetInput(scoredGrid);
  clusterFilter->SetMeshing(false);
  clusterFilter->SetMinPts(4);
  clusterFilter->Seteps(spacing);
  clusterFilter->Update();

  mitk::ProgressBar::GetInstance()->Progress();

  // Create plane suggestion
  mitk::BaseRenderer::Pointer br =
    mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget0"));
  mitk::PlaneProposer planeProposer;
  std::vector<mitk::UnstructuredGrid::Pointer> grids = clusterFilter->GetAllClusters();

  planeProposer.SetUnstructuredGrids(grids);
  mitk::SliceNavigationController::Pointer snc = br->GetSliceNavigationController();
  planeProposer.SetSliceNavigationController(snc);
  planeProposer.SetUseDistances(true);
  try
  {
    planeProposer.CreatePlaneInfo();
  }
  catch (const mitk::Exception &e)
  {
    MITK_ERROR << e.what();
  }

  mitk::RenderingManager::GetInstance()->RequestUpdateAll();

  m_FirstRun = false;
}

void QmitkSlicesInterpolator::OnReinit3DInterpolation()
{
  mitk::NodePredicateProperty::Pointer pred =
    mitk::NodePredicateProperty::New("3DContourContainer", mitk::BoolProperty::New(true));
  mitk::DataStorage::SetOfObjects::ConstPointer contourNodes =
    m_DataStorage->GetDerivations(m_ToolManager->GetWorkingData(0), pred);

  if (contourNodes->Size() != 0)
  {
    m_BtnApply3D->setEnabled(true);
    m_3DContourNode = contourNodes->at(0);
    mitk::Surface::Pointer contours = dynamic_cast<mitk::Surface *>(m_3DContourNode->GetData());
    if (contours)
      mitk::SurfaceInterpolationController::GetInstance()->ReinitializeInterpolation(contours);
    m_BtnReinit3DInterpolation->setEnabled(false);
  }
  else
  {
    m_BtnApply3D->setEnabled(false);
    QMessageBox errorInfo;
    errorInfo.setWindowTitle("Reinitialize surface interpolation");
    errorInfo.setIcon(QMessageBox::Information);
    errorInfo.setText("No contours available for the selected segmentation!");
    errorInfo.exec();
  }
}

void QmitkSlicesInterpolator::OnAcceptAllPopupActivated(QAction *action)
{
  try
  {
    std::map<QAction *, mitk::SliceNavigationController *>::const_iterator iter = ACTION_TO_SLICEDIMENSION.find(action);
    if (iter != ACTION_TO_SLICEDIMENSION.end())
    {
      mitk::SliceNavigationController *slicer = iter->second;
      AcceptAllInterpolations(slicer);
    }
  }
  catch (...)
  {
    /* Showing message box with possible memory error */
    QMessageBox errorInfo;
    errorInfo.setWindowTitle("Interpolation Process");
    errorInfo.setIcon(QMessageBox::Critical);
    errorInfo.setText("An error occurred during interpolation. Possible cause: Not enough memory!");
    errorInfo.exec();

    // additional error message on std::cerr
    std::cerr << "Ill construction in " __FILE__ " l. " << __LINE__ << std::endl;
  }
}

void QmitkSlicesInterpolator::OnInterpolationActivated(bool on)
{
  m_2DInterpolationEnabled = on;

  try
  {
    if (m_DataStorage.IsNotNull())
    {
      if (on && !m_DataStorage->Exists(m_FeedbackNode))
      {
        m_DataStorage->Add(m_FeedbackNode);
      }
    }
  }
  catch (...)
  {
    // don't care (double add/remove)
  }

  if (m_ToolManager)
  {
    mitk::DataNode *workingNode = m_ToolManager->GetWorkingData(0);
    mitk::DataNode *referenceNode = m_ToolManager->GetReferenceData(0);
    QWidget::setEnabled(workingNode != nullptr);

    m_BtnApply2D->setEnabled(on);
    m_FeedbackNode->SetVisibility(on);

    if (!on)
    {
      mitk::RenderingManager::GetInstance()->RequestUpdateAll();
      return;
    }

    if (workingNode)
    {
      mitk::Image *segmentation = dynamic_cast<mitk::Image *>(workingNode->GetData());
      if (segmentation)
      {
        m_Interpolator->SetSegmentationVolume(segmentation);

        if (referenceNode)
        {
          mitk::Image *referenceImage = dynamic_cast<mitk::Image *>(referenceNode->GetData());
          m_Interpolator->SetReferenceVolume(referenceImage); // may be nullptr
        }
      }
    }
  }

  UpdateVisibleSuggestion();
}

void QmitkSlicesInterpolator::Run3DInterpolation()
{
  m_SurfaceInterpolator->Interpolate();
}

void QmitkSlicesInterpolator::StartUpdateInterpolationTimer()
{
  m_Timer->start(500);
}

void QmitkSlicesInterpolator::StopUpdateInterpolationTimer()
{
  m_Timer->stop();
  m_InterpolatedSurfaceNode->SetProperty("color", mitk::ColorProperty::New(SURFACE_COLOR_RGB));
  mitk::RenderingManager::GetInstance()->RequestUpdate(
    mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget3"))->GetRenderWindow());
}

void QmitkSlicesInterpolator::ChangeSurfaceColor()
{
  float currentColor[3];
  m_InterpolatedSurfaceNode->GetColor(currentColor);

  if (currentColor[2] == SURFACE_COLOR_RGB[2])
  {
    m_InterpolatedSurfaceNode->SetProperty("color", mitk::ColorProperty::New(1.0f, 1.0f, 1.0f));
  }
  else
  {
    m_InterpolatedSurfaceNode->SetProperty("color", mitk::ColorProperty::New(SURFACE_COLOR_RGB));
  }
  m_InterpolatedSurfaceNode->Update();
  mitk::RenderingManager::GetInstance()->RequestUpdate(
    mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget3"))->GetRenderWindow());
}

void QmitkSlicesInterpolator::On3DInterpolationActivated(bool on)
{
  m_3DInterpolationEnabled = on;

  this->CheckSupportedImageDimension();
  try
  {
    if (m_DataStorage.IsNotNull() && m_ToolManager && m_3DInterpolationEnabled)
    {
      mitk::DataNode *workingNode = m_ToolManager->GetWorkingData(0);

      if (workingNode)
      {
        if ((workingNode->IsVisible(mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget2")))))
        {
          int ret = QMessageBox::Yes;

          if (m_SurfaceInterpolator->EstimatePortionOfNeededMemory() > 0.5)
          {
            QMessageBox msgBox;
            msgBox.setText("Due to short handed system memory the 3D interpolation may be very slow!");
            msgBox.setInformativeText("Are you sure you want to activate the 3D interpolation?");
            msgBox.setStandardButtons(QMessageBox::No | QMessageBox::Yes);
            ret = msgBox.exec();
          }

          if (m_Watcher.isRunning())
            m_Watcher.waitForFinished();

          if (ret == QMessageBox::Yes)
          {
            m_Future = QtConcurrent::run(this, &QmitkSlicesInterpolator::Run3DInterpolation);
            m_Watcher.setFuture(m_Future);
          }
          else
          {
            m_CmbInterpolation->setCurrentIndex(0);
          }
        }
      }
      else
      {
        QWidget::setEnabled(false);
        m_ChkShowPositionNodes->setEnabled(m_3DInterpolationEnabled);
      }
    }
    if (!m_3DInterpolationEnabled)
    {
      this->Show3DInterpolationResult(false);
      m_BtnApply3D->setEnabled(m_3DInterpolationEnabled);

      // T28261
      // m_BtnSuggestPlane->setEnabled(m_3DInterpolationEnabled);
    }
  }
  catch (...)
  {
    MITK_ERROR << "Error with 3D surface interpolation!";
  }
  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}

void QmitkSlicesInterpolator::EnableInterpolation(bool on)
{
  // only to be called from the outside world
  // just a redirection to OnInterpolationActivated
  OnInterpolationActivated(on);
}

void QmitkSlicesInterpolator::Enable3DInterpolation(bool on)
{
  // only to be called from the outside world
  // just a redirection to OnInterpolationActivated
  On3DInterpolationActivated(on);
}

void QmitkSlicesInterpolator::UpdateVisibleSuggestion()
{
  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}

void QmitkSlicesInterpolator::OnInterpolationInfoChanged(const itk::EventObject & /*e*/)
{
  // something (e.g. undo) changed the interpolation info, we should refresh our display
  UpdateVisibleSuggestion();
}

void QmitkSlicesInterpolator::OnInterpolationAborted(const itk::EventObject& /*e*/)
{
  m_CmbInterpolation->setCurrentIndex(0);
  m_FeedbackNode->SetData(nullptr);
}

void QmitkSlicesInterpolator::OnSurfaceInterpolationInfoChanged(const itk::EventObject & /*e*/)
{
  if (m_3DInterpolationEnabled)
  {
    if (m_Watcher.isRunning())
      m_Watcher.waitForFinished();
    m_Future = QtConcurrent::run(this, &QmitkSlicesInterpolator::Run3DInterpolation);
    m_Watcher.setFuture(m_Future);
  }
}

void QmitkSlicesInterpolator::SetCurrentContourListID()
{
  // New ContourList = hide current interpolation
  Show3DInterpolationResult(false);

  if (m_DataStorage.IsNotNull() && m_ToolManager && m_LastSNC)
  {
    mitk::DataNode *workingNode = m_ToolManager->GetWorkingData(0);

    if (workingNode)
    {
      QWidget::setEnabled(true);

      const auto timePoint = m_LastSNC->GetSelectedTimePoint();
      // In case the time is not valid use 0 to access the time geometry of the working node
      unsigned int time_position = 0;
      if (!workingNode->GetData()->GetTimeGeometry()->IsValidTimePoint(timePoint))
      {
        MITK_WARN << "Cannot accept interpolation. Time point selected by SliceNavigationController is not within the time bounds of WorkingImage. Time point: " << timePoint;
        return;
      }
      time_position = workingNode->GetData()->GetTimeGeometry()->TimePointToTimeStep(timePoint);

      mitk::Vector3D spacing = workingNode->GetData()->GetGeometry(time_position)->GetSpacing();
      double minSpacing(100);
      double maxSpacing(0);
      for (int i = 0; i < 3; i++)
      {
        if (spacing[i] < minSpacing)
        {
          minSpacing = spacing[i];
        }
        if (spacing[i] > maxSpacing)
        {
          maxSpacing = spacing[i];
        }
      }

      m_SurfaceInterpolator->SetMaxSpacing(maxSpacing);
      m_SurfaceInterpolator->SetMinSpacing(minSpacing);
      m_SurfaceInterpolator->SetDistanceImageVolume(50000);

      mitk::Image *segmentationImage = dynamic_cast<mitk::Image *>(workingNode->GetData());

      m_SurfaceInterpolator->SetCurrentInterpolationSession(segmentationImage);
      m_SurfaceInterpolator->SetCurrentTimePoint(timePoint);

      if (m_3DInterpolationEnabled)
      {
        if (m_Watcher.isRunning())
          m_Watcher.waitForFinished();
        m_Future = QtConcurrent::run(this, &QmitkSlicesInterpolator::Run3DInterpolation);
        m_Watcher.setFuture(m_Future);
      }
    }
    else
    {
      QWidget::setEnabled(false);
    }
  }
}

void QmitkSlicesInterpolator::Show3DInterpolationResult(bool status)
{
  if (m_InterpolatedSurfaceNode.IsNotNull())
    m_InterpolatedSurfaceNode->SetVisibility(status);

  if (m_3DContourNode.IsNotNull())
    m_3DContourNode->SetVisibility(
      status, mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget3")));

  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}

void QmitkSlicesInterpolator::CheckSupportedImageDimension()
{
  if (m_ToolManager->GetWorkingData(0))
    m_Segmentation = dynamic_cast<mitk::Image *>(m_ToolManager->GetWorkingData(0)->GetData());

  /*if (m_3DInterpolationEnabled && m_Segmentation && m_Segmentation->GetDimension() != 3)
  {
    QMessageBox info;
    info.setWindowTitle("3D Interpolation Process");
    info.setIcon(QMessageBox::Information);
    info.setText("3D Interpolation is only supported for 3D images at the moment!");
    info.exec();
    m_CmbInterpolation->setCurrentIndex(0);
  }*/
}

void QmitkSlicesInterpolator::OnSliceNavigationControllerDeleted(const itk::Object *sender,
                                                                 const itk::EventObject & /*e*/)
{
  // Don't know how to avoid const_cast here?!
  mitk::SliceNavigationController *slicer =
    dynamic_cast<mitk::SliceNavigationController *>(const_cast<itk::Object *>(sender));
  if (slicer)
  {
    m_ControllerToTimeObserverTag.remove(slicer);
    m_ControllerToSliceObserverTag.remove(slicer);
    m_ControllerToDeleteObserverTag.remove(slicer);
  }
}

void QmitkSlicesInterpolator::WaitForFutures()
{
  if (m_Watcher.isRunning())
  {
    m_Watcher.waitForFinished();
  }

  if (m_PlaneWatcher.isRunning())
  {
    m_PlaneWatcher.waitForFinished();
  }
}

void QmitkSlicesInterpolator::NodeRemoved(const mitk::DataNode* node)
{
  if ((m_ToolManager && m_ToolManager->GetWorkingData(0) == node) ||
      node == m_3DContourNode ||
      node == m_FeedbackNode ||
      node == m_InterpolatedSurfaceNode)
  {
    WaitForFutures();
  }
}
