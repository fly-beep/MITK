/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

#include "mitkPaintbrushTool.h"

#include "mitkAbstractTransformGeometry.h"
#include "mitkBaseRenderer.h"
#include "mitkToolManager.h"

#include "mitkContourModelUtils.h"
#include "mitkLevelWindowProperty.h"
#include "mitkImageWriteAccessor.h"

int mitk::PaintbrushTool::m_Size = 1;

mitk::PaintbrushTool::PaintbrushTool(int paintingPixelValue)
  : FeedbackContourTool("PressMoveReleaseWithCTRLInversionAllMouseMoves"),
    m_PaintingPixelValue(paintingPixelValue),
    m_LastContourSize(0) // other than initial mitk::PaintbrushTool::m_Size (around l. 28)
{
  m_MasterContour = ContourModel::New();
  m_MasterContour->Initialize();
  m_CurrentPlane = nullptr;
}

mitk::PaintbrushTool::~PaintbrushTool()
{
}

void mitk::PaintbrushTool::ConnectActionsAndFunctions()
{
  CONNECT_FUNCTION("PrimaryButtonPressed", OnMousePressed);
  CONNECT_FUNCTION("Move", OnPrimaryButtonPressedMoved);
  CONNECT_FUNCTION("MouseMove", OnMouseMoved);
  CONNECT_FUNCTION("Release", OnMouseReleased);
  CONNECT_FUNCTION("InvertLogic", OnInvertLogic);
}

void mitk::PaintbrushTool::Activated()
{
  Superclass::Activated();

  FeedbackContourTool::SetFeedbackContourVisible(true);
  SizeChanged.Send(m_Size);
  this->GetToolManager()->WorkingDataChanged +=
    mitk::MessageDelegate<mitk::PaintbrushTool>(this, &mitk::PaintbrushTool::OnToolManagerWorkingDataModified);

  m_PaintingNode = DataNode::New();
  m_PaintingNode->SetProperty("levelwindow", mitk::LevelWindowProperty::New(mitk::LevelWindow(0, m_InternalFillValue)));
  m_PaintingNode->SetProperty("binary", mitk::BoolProperty::New(true));

  m_PaintingNode->SetProperty("outline binary", mitk::BoolProperty::New(true));
  m_PaintingNode->SetProperty("name", mitk::StringProperty::New("Paintbrush_Node"));
  m_PaintingNode->SetProperty("helper object", mitk::BoolProperty::New(true));
  m_PaintingNode->SetProperty("opacity", mitk::FloatProperty::New(0.8));
  m_PaintingNode->SetProperty("includeInBoundingBox", mitk::BoolProperty::New(false));
  m_PaintingNode->SetVisibility(
    false, mitk::BaseRenderer::GetInstance(mitk::BaseRenderer::GetRenderWindowByName("stdmulti.widget3")));

  this->GetToolManager()->GetDataStorage()->Add(m_PaintingNode);
}

void mitk::PaintbrushTool::Deactivated()
{
  FeedbackContourTool::SetFeedbackContourVisible(false);
  if (this->GetToolManager()->GetDataStorage()->Exists(m_PaintingNode))
    this->GetToolManager()->GetDataStorage()->Remove(m_PaintingNode);
  m_WorkingSlice = nullptr;
  m_PaintingSlice = nullptr;
  m_CurrentPlane = nullptr;
  m_PaintingNode = nullptr;

  this->GetToolManager()->WorkingDataChanged -=
    mitk::MessageDelegate<mitk::PaintbrushTool>(this, &mitk::PaintbrushTool::OnToolManagerWorkingDataModified);

  Superclass::Deactivated();
}

void mitk::PaintbrushTool::SetSize(int value)
{
  m_Size = value;
}

mitk::Point2D mitk::PaintbrushTool::upperLeft(mitk::Point2D p)
{
  p[0] -= 0.5;
  p[1] += 0.5;
  return p;
}

void mitk::PaintbrushTool::UpdateContour(const InteractionPositionEvent *positionEvent)
{
  // MITK_INFO<<"Update...";
  // examine stateEvent and create a contour that matches the pixel mask that we are going to draw
  // mitk::InteractionPositionEvent* positionEvent = dynamic_cast<mitk::InteractionPositionEvent*>( interactionEvent );
  // const PositionEvent* positionEvent = dynamic_cast<const PositionEvent*>(stateEvent->GetEvent());
  if (!positionEvent)
    return;

  // Get Spacing of current Slice
  // mitk::Vector3D vSpacing = m_WorkingSlice->GetSlicedGeometry()->GetPlaneGeometry(0)->GetSpacing();

  //
  // Draw a contour in Square according to selected brush size
  //
  int radius = (m_Size) / 2;
  float fradius = static_cast<float>(m_Size) / 2.0f;

  ContourModel::Pointer contourInImageIndexCoordinates = ContourModel::New();

  // estimate center point of the brush ( relative to the pixel the mouse points on )
  // -- left upper corner for even sizes,
  // -- midpoint for uneven sizes
  mitk::Point2D centerCorrection;
  centerCorrection.Fill(0);

  // even --> correction of [+0.5, +0.5]
  bool evenSize = ((m_Size % 2) == 0);
  if (evenSize)
  {
    centerCorrection[0] += 0.5;
    centerCorrection[1] += 0.5;
  }

  // we will compute the control points for the upper left quarter part of a circle contour
  std::vector<mitk::Point2D> quarterCycleUpperRight;
  std::vector<mitk::Point2D> quarterCycleLowerRight;
  std::vector<mitk::Point2D> quarterCycleLowerLeft;
  std::vector<mitk::Point2D> quarterCycleUpperLeft;

  mitk::Point2D curPoint;
  bool curPointIsInside = true;
  curPoint[0] = 0;
  curPoint[1] = radius;
  quarterCycleUpperRight.push_back(upperLeft(curPoint));

  // to estimate if a pixel is inside the circle, we need to compare against the 'outer radius'
  // i.e. the distance from the midpoint [0,0] to the border of the pixel [0,radius]
  // const float outer_radius = static_cast<float>(radius) + 0.5;

  while (curPoint[1] > 0)
  {
    // Move right until pixel is outside circle
    float curPointX_squared = 0.0f;
    float curPointY_squared = (curPoint[1] - centerCorrection[1]) * (curPoint[1] - centerCorrection[1]);
    while (curPointIsInside)
    {
      // increment posX and chec
      curPoint[0]++;
      curPointX_squared = (curPoint[0] - centerCorrection[0]) * (curPoint[0] - centerCorrection[0]);
      const float len = sqrt(curPointX_squared + curPointY_squared);
      if (len > fradius)
      {
        // found first Pixel in this horizontal line, that is outside the circle
        curPointIsInside = false;
      }
    }
    quarterCycleUpperRight.push_back(upperLeft(curPoint));

    // Move down until pixel is inside circle
    while (!curPointIsInside)
    {
      // increment posX and chec
      curPoint[1]--;
      curPointY_squared = (curPoint[1] - centerCorrection[1]) * (curPoint[1] - centerCorrection[1]);
      const float len = sqrt(curPointX_squared + curPointY_squared);
      if (len <= fradius)
      {
        // found first Pixel in this horizontal line, that is outside the circle
        curPointIsInside = true;
        quarterCycleUpperRight.push_back(upperLeft(curPoint));
      }

      // Quarter cycle is full, when curPoint y position is 0
      if (curPoint[1] <= 0)
        break;
    }
  }

  // QuarterCycle is full! Now copy quarter cycle to other quarters.

  if (!evenSize)
  {
    std::vector<mitk::Point2D>::const_iterator it = quarterCycleUpperRight.begin();
    while (it != quarterCycleUpperRight.end())
    {
      mitk::Point2D p;
      p = *it;

      // the contour points in the lower right corner have same position but with negative y values
      p[1] *= -1;
      quarterCycleLowerRight.push_back(p);

      // the contour points in the lower left corner have same position
      // but with both x,y negative
      p[0] *= -1;
      quarterCycleLowerLeft.push_back(p);

      // the contour points in the upper left corner have same position
      // but with x negative
      p[1] *= -1;
      quarterCycleUpperLeft.push_back(p);

      it++;
    }
  }
  else
  {
    std::vector<mitk::Point2D>::const_iterator it = quarterCycleUpperRight.begin();
    while (it != quarterCycleUpperRight.end())
    {
      mitk::Point2D p, q;
      p = *it;

      q = p;
      // the contour points in the lower right corner have same position but with negative y values
      q[1] *= -1;
      // correct for moved offset if size even = the midpoint is not the midpoint of the current pixel
      // but its upper rigt corner
      q[1] += 1;
      quarterCycleLowerRight.push_back(q);

      q = p;
      // the contour points in the lower left corner have same position
      // but with both x,y negative
      q[1] = -1.0f * q[1] + 1;
      q[0] = -1.0f * q[0] + 1;
      quarterCycleLowerLeft.push_back(q);

      // the contour points in the upper left corner have same position
      // but with x negative
      q = p;
      q[0] *= -1;
      q[0] += 1;
      quarterCycleUpperLeft.push_back(q);

      it++;
    }
  }

  // fill contour with poins in right ordering, starting with the upperRight block
  mitk::Point3D tempPoint;
  for (unsigned int i = 0; i < quarterCycleUpperRight.size(); i++)
  {
    tempPoint[0] = quarterCycleUpperRight[i][0];
    tempPoint[1] = quarterCycleUpperRight[i][1];
    tempPoint[2] = 0;
    contourInImageIndexCoordinates->AddVertex(tempPoint);
  }
  // the lower right has to be parsed in reverse order
  for (int i = quarterCycleLowerRight.size() - 1; i >= 0; i--)
  {
    tempPoint[0] = quarterCycleLowerRight[i][0];
    tempPoint[1] = quarterCycleLowerRight[i][1];
    tempPoint[2] = 0;
    contourInImageIndexCoordinates->AddVertex(tempPoint);
  }
  for (unsigned int i = 0; i < quarterCycleLowerLeft.size(); i++)
  {
    tempPoint[0] = quarterCycleLowerLeft[i][0];
    tempPoint[1] = quarterCycleLowerLeft[i][1];
    tempPoint[2] = 0;
    contourInImageIndexCoordinates->AddVertex(tempPoint);
  }
  // the upper left also has to be parsed in reverse order
  for (int i = quarterCycleUpperLeft.size() - 1; i >= 0; i--)
  {
    tempPoint[0] = quarterCycleUpperLeft[i][0];
    tempPoint[1] = quarterCycleUpperLeft[i][1];
    tempPoint[2] = 0;
    contourInImageIndexCoordinates->AddVertex(tempPoint);
  }

  m_MasterContour = contourInImageIndexCoordinates;
}

void mitk::PaintbrushTool::OnMousePressed(StateMachineAction *, InteractionEvent *interactionEvent)
{
  if (m_WorkingSlice.IsNull())
    return;

  auto *positionEvent = dynamic_cast<mitk::InteractionPositionEvent *>(interactionEvent);
  if (!positionEvent)
    return;

  m_WorkingSlice->GetGeometry()->WorldToIndex(positionEvent->GetPositionInWorld(), m_LastPosition);

  this->m_PaintingNode->SetVisibility(true);

  m_LastEventSender = positionEvent->GetSender();
  m_LastEventSlice = m_LastEventSender->GetSlice();
  m_PaintingSlice = nullptr; //force reset of the painting slice. Will be triggered in MouseMoved() by
                             //CheckIfCurrentSliceHasChanged

  m_MasterContour->SetClosed(true);
  this->MouseMoved(interactionEvent, true);
}

void mitk::PaintbrushTool::OnMouseMoved(StateMachineAction *, InteractionEvent *interactionEvent)
{
  MouseMoved(interactionEvent, false);
}

void mitk::PaintbrushTool::OnPrimaryButtonPressedMoved(StateMachineAction *, InteractionEvent *interactionEvent)
{
  MouseMoved(interactionEvent, true);
}

/**
  Insert the point to the feedback contour,finish to build the contour and at the same time the painting function
  */
void mitk::PaintbrushTool::MouseMoved(mitk::InteractionEvent *interactionEvent, bool leftMouseButtonPressed)
{
  auto *positionEvent = dynamic_cast<mitk::InteractionPositionEvent *>(interactionEvent);

  CheckIfCurrentSliceHasChanged(positionEvent);

  if (m_LastContourSize != m_Size)
  {
    UpdateContour(positionEvent);
    m_LastContourSize = m_Size;
  }

  Point3D worldCoordinates = positionEvent->GetPositionInWorld();
  Point3D indexCoordinates;

  m_WorkingSlice->GetGeometry()->WorldToIndex(worldCoordinates, indexCoordinates);

  // round to nearest voxel center (abort if this hasn't changed)
  if (m_Size % 2 == 0) // even
  {
    indexCoordinates[0] = std::round(indexCoordinates[0]);
    indexCoordinates[1] = std::round(indexCoordinates[1]);
  }
  else // odd
  {
    indexCoordinates[0] = std::round(indexCoordinates[0]);
    indexCoordinates[1] = std::round(indexCoordinates[1]);
  }

  static Point3D lastPos; // uninitialized: if somebody finds out how this can be initialized in a one-liner, tell me
  if (fabs(indexCoordinates[0] - lastPos[0]) > mitk::eps || fabs(indexCoordinates[1] - lastPos[1]) > mitk::eps ||
      fabs(indexCoordinates[2] - lastPos[2]) > mitk::eps || leftMouseButtonPressed)
  {
    lastPos = indexCoordinates;
  }
  else
  {
    return;
  }

  auto contour = ContourModel::New();
  contour->SetClosed(true);

  auto it = m_MasterContour->Begin();
  auto end = m_MasterContour->End();

  while (it != end)
  {
    auto point = (*it)->Coordinates;
    point[0] += indexCoordinates[0];
    point[1] += indexCoordinates[1];

    contour->AddVertex(point);
    ++it;
  }

  if (leftMouseButtonPressed)
  {
    ContourModelUtils::FillContourInSlice2(contour, m_PaintingSlice, m_InternalFillValue);

    const double dist = indexCoordinates.EuclideanDistanceTo(m_LastPosition);
    const double radius = static_cast<double>(m_Size) / 2.0;

    // if points are >= radius away draw rectangle to fill empty holes
    // in between the 2 points
    if (dist > radius)
    {
      const mitk::Point3D &currentPos = indexCoordinates;
      mitk::Point3D direction;
      mitk::Point3D vertex;
      mitk::Point3D normal;

      direction[0] = indexCoordinates[0] - m_LastPosition[0];
      direction[1] = indexCoordinates[1] - m_LastPosition[1];
      direction[2] = indexCoordinates[2] - m_LastPosition[2];

      direction[0] = direction.GetVnlVector().normalize()[0];
      direction[1] = direction.GetVnlVector().normalize()[1];
      direction[2] = direction.GetVnlVector().normalize()[2];

      // 90 degrees rotation of direction
      normal[0] = -1.0 * direction[1];
      normal[1] = direction[0];

      auto gapContour = ContourModel::New();

      // upper left corner
      vertex[0] = m_LastPosition[0] + (normal[0] * radius);
      vertex[1] = m_LastPosition[1] + (normal[1] * radius);

      gapContour->AddVertex(vertex);

      // upper right corner
      vertex[0] = currentPos[0] + (normal[0] * radius);
      vertex[1] = currentPos[1] + (normal[1] * radius);

      gapContour->AddVertex(vertex);

      // lower right corner
      vertex[0] = currentPos[0] - (normal[0] * radius);
      vertex[1] = currentPos[1] - (normal[1] * radius);

      gapContour->AddVertex(vertex);

      // lower left corner
      vertex[0] = m_LastPosition[0] - (normal[0] * radius);
      vertex[1] = m_LastPosition[1] - (normal[1] * radius);

      gapContour->AddVertex(vertex);

      ContourModelUtils::FillContourInSlice2(gapContour, m_PaintingSlice, m_InternalFillValue);
    }
  }
  else
  {
    // switched from different renderwindow
    // no activate hover highlighting. Otherwise undo / redo wont work
    this->m_PaintingNode->SetVisibility(false);
  }

  m_LastPosition = indexCoordinates;

  // visualize contour
  ContourModel::Pointer tmp =
    FeedbackContourTool::BackProjectContourFrom2DSlice(m_WorkingSlice->GetGeometry(), contour);

  this->UpdateCurrentFeedbackContour(tmp);

  assert(positionEvent->GetSender()->GetRenderWindow());
  RenderingManager::GetInstance()->RequestUpdate(positionEvent->GetSender()->GetRenderWindow());
}

void mitk::PaintbrushTool::OnMouseReleased(StateMachineAction *, InteractionEvent *interactionEvent)
{
  // When mouse is released write segmentationresult back into image
  auto *positionEvent = dynamic_cast<mitk::InteractionPositionEvent *>(interactionEvent);
  if (!positionEvent)
    return;

  DataNode* workingNode(this->GetToolManager()->GetWorkingData(0));
  auto workingImage = dynamic_cast<LabelSetImage*>(workingNode->GetData());
  int activePixelValue = ContourModelUtils::GetActivePixelValue(workingImage);

  //as paintbrush tools should always allow to manipulate active label
  //(that is what the user expects/knows when using tools so far:
  //the active label can always be changed even if locked)
  //we realize that by cloning the relevant label set and changing the lock state
  //this fillLabelSet is used for the transfer.
  auto fillLabelSet = workingImage->GetActiveLabelSet()->Clone();
  auto activeLabelClone = fillLabelSet->GetLabel(workingImage->GetActiveLabel()->GetValue());
  if (nullptr != activeLabelClone)
  {
    activeLabelClone->SetLocked(false);
  }

  TransferLabelContent(m_PaintingSlice, m_WorkingSlice, fillLabelSet, 0, workingImage->GetExteriorLabel()->GetValue(), false, { {m_InternalFillValue, m_PaintingPixelValue * activePixelValue} }, mitk::MultiLabelSegmentation::MergeStyle::Merge);

  this->WriteBackSegmentationResult(positionEvent, m_WorkingSlice->Clone());

  // deactivate visibility of helper node
  m_PaintingNode->SetVisibility(false);

  RenderingManager::GetInstance()->RequestUpdate(positionEvent->GetSender()->GetRenderWindow());
}

/**
  Called when the CTRL key is pressed. Will change the painting pixel value from 0 to 1 or from 1 to 0.
  */
void mitk::PaintbrushTool::OnInvertLogic(StateMachineAction *, InteractionEvent *)
{
  // Inversion only for 0 and 1 as painting values
  if (m_PaintingPixelValue == 1)
  {
    m_PaintingPixelValue = 0;
    FeedbackContourTool::SetFeedbackContourColor(1.0, 0.0, 0.0);
  }
  else if (m_PaintingPixelValue == 0)
  {
    m_PaintingPixelValue = 1;
    FeedbackContourTool::SetFeedbackContourColorDefault();
  }
  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}

void mitk::PaintbrushTool::CheckIfCurrentSliceHasChanged(const InteractionPositionEvent *event)
{
  const PlaneGeometry* planeGeometry((event->GetSender()->GetCurrentWorldPlaneGeometry()));
  const auto* abstractTransformGeometry(
    dynamic_cast<const AbstractTransformGeometry *>(event->GetSender()->GetCurrentWorldPlaneGeometry()));
  if (nullptr == planeGeometry || nullptr != abstractTransformGeometry)
  {
    return;
  }

  DataNode* workingNode = this->GetToolManager()->GetWorkingData(0);
  if (nullptr == workingNode)
  {
    return;
  }

  Image::Pointer image = dynamic_cast<Image *>(workingNode->GetData());
  if (nullptr == image)
  {
    return;
  }

  if (m_CurrentPlane.IsNull() || m_WorkingSlice.IsNull()
      //or not the same slice
     || !mitk::MatrixEqualElementWise(planeGeometry->GetIndexToWorldTransform()->GetMatrix(),
       m_CurrentPlane->GetIndexToWorldTransform()->GetMatrix())
     || !mitk::Equal(planeGeometry->GetIndexToWorldTransform()->GetOffset(),
       m_CurrentPlane->GetIndexToWorldTransform()->GetOffset()))
  {
    m_CurrentPlane = planeGeometry;
    m_WorkingSlice = SegTool2D::GetAffectedImageSliceAs2DImage(event, image)->Clone();
  }

  if (m_PaintingSlice.IsNull())
  {
    m_PaintingSlice = Image::New();
    m_PaintingSlice->Initialize(m_WorkingSlice);

    unsigned int byteSize = m_PaintingSlice->GetPixelType().GetSize();
    for (unsigned int dim = 0; dim < m_PaintingSlice->GetDimension(); ++dim)
    {
      byteSize *= m_PaintingSlice->GetDimension(dim);
    }
    mitk::ImageWriteAccessor writeAccess(m_PaintingSlice.GetPointer(), m_PaintingSlice->GetVolumeData(0));
    memset(writeAccess.GetData(), 0, byteSize);

    m_PaintingNode->SetData(m_PaintingSlice);
  }

  mitk::Color currentColor;
  if (m_PaintingPixelValue == 1)
  {
    currentColor.Set(0.0, 1.0, 0.);
  }
  else
  {
    currentColor.Set(1.0, 0.0, 0.);
  }
  m_PaintingNode->SetProperty("color", mitk::ColorProperty::New(currentColor[0], currentColor[1], currentColor[2]));
}

void mitk::PaintbrushTool::OnToolManagerWorkingDataModified()
{
  // Here we simply set the current working slice to null. The next time the mouse is moved
  // within a renderwindow a new slice will be extracted from the new working data
  m_WorkingSlice = nullptr;
  m_PaintingSlice = nullptr;
}
