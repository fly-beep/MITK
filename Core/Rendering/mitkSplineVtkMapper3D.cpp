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


#include "mitkSplineVtkMapper3D.h"
#include <vtkProp.h>
#include <vtkPropAssembly.h>
#include <vtkCardinalSpline.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkCellArray.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkTubeFilter.h>
#include <vtkPropCollection.h>
#include <mitkProperties.h>
#include <mitkPointSet.h>


mitk::SplineVtkMapper3D::SplineVtkMapper3D()
: m_SplinesAvailable (false), m_SplinesAddedToAssembly(false) 
{
  m_SplinesActor = vtkActor::New();
  m_SplineAssembly = vtkPropAssembly::New();
}


mitk::SplineVtkMapper3D::~SplineVtkMapper3D()
{
  m_SplinesActor->Delete();
  m_SplineAssembly->Delete();
}

vtkProp*
mitk::SplineVtkMapper3D::GetProp()
{
  if (GetDataTreeNode() == NULL)
    return NULL; 

  //to assign User Transforms in superclass
  Superclass::GetProp();

  m_SplinesActor->SetUserTransform( GetDataTreeNode()->GetVtkTransform() );
  
  return m_SplineAssembly;
}


void
mitk::SplineVtkMapper3D::GenerateData()
{
  Superclass::GenerateData();
  mitk::PointSet::Pointer input = const_cast<mitk::PointSet*>( this->GetInput( ) );
//  input->Update();//already done in superclass

  // Number of points on the spline
  unsigned int numberOfOutputPoints = 400;
  unsigned int numberOfInputPoints = input->GetSize();


  if ( numberOfInputPoints >= 2 )
  {
    m_SplinesAvailable = true;
    vtkCardinalSpline* splineX = vtkCardinalSpline::New();
    vtkCardinalSpline* splineY = vtkCardinalSpline::New();
    vtkCardinalSpline* splineZ = vtkCardinalSpline::New();

    for ( unsigned int i = 0 ; i < numberOfInputPoints; ++i )
    {
      mitk::PointSet::PointType point = input->GetPoint( i );
      splineX->AddPoint( i, point[ 0 ] );
      splineY->AddPoint( i, point[ 1 ] );
      splineZ->AddPoint( i, point[ 2 ] );
    }
    vtkPoints* points = vtkPoints::New();
    vtkPolyData* profileData = vtkPolyData::New();


    // Interpolate x, y and z by using the three spline filters and
    // create new points
    double t = 0.0f;
    for ( unsigned int i = 0; i < numberOfOutputPoints; ++i )
    {
      t = ( ( ( ( double ) numberOfInputPoints ) - 1.0f ) / ( ( ( double ) numberOfOutputPoints ) - 1.0f ) ) * ( ( double ) i );
      points->InsertPoint( i, splineX->Evaluate( t ), splineY->Evaluate( t ), splineZ->Evaluate( t ) ) ;
    }

    // Create the polyline.
    vtkCellArray* lines = vtkCellArray::New();
    lines->InsertNextCell( numberOfOutputPoints );
    for ( unsigned int i = 0; i < numberOfOutputPoints; ++i )
      lines->InsertCellPoint( i );

    profileData->SetPoints( points );
    profileData->SetLines( lines );

    // Add thickness to the resulting line.
    //vtkTubeFilter* profileTubes = vtkTubeFilter::New();
    //profileTubes->SetNumberOfSides(8);
    //profileTubes->SetInput(profileData);
    //profileTubes->SetRadius(.005);

    vtkPolyDataMapper* profileMapper = vtkPolyDataMapper::New();
    profileMapper->SetInput( profileData );

    m_SplinesActor->SetMapper( profileMapper );


#if ((VTK_MAJOR_VERSION > 4) || ((VTK_MAJOR_VERSION==4) && (VTK_MINOR_VERSION>=4) ))
    double rgba[ 4 ] = {1.0f, 0.0f, 0.0f, 1.0f};
#else
    float rgba[ 4 ] = {1.0f, 0.0f, 0.0f, 1.0f};
#endif
    this->GetDataTreeNode()->GetColor( (float*)rgba, NULL );
    m_SplinesActor->GetProperty()->SetColor( rgba );

    float lineWidth;
    if (dynamic_cast<mitk::FloatProperty *>(this->GetDataTreeNode()->GetProperty("linewidth").GetPointer()) == NULL)
      lineWidth = 1.0;
    else
      lineWidth = dynamic_cast<mitk::FloatProperty *>(this->GetDataTreeNode()->GetProperty("linewidth").GetPointer())->GetValue();
    m_SplinesActor->GetProperty()->SetLineWidth(lineWidth);
  }
  else
  {
    m_SplinesAvailable = false;
  }


  if ( m_SplinesAvailable )
  {
    if ( ! m_SplinesAddedToAssembly )
    {
      m_SplineAssembly->AddPart( m_SplinesActor );
      m_SplinesAddedToAssembly = true;
    }
  }
  else
  {
    if ( m_SplinesAddedToAssembly )
    {
      m_SplineAssembly->RemovePart( m_SplinesActor );
      m_SplinesAddedToAssembly = false; 
    }
  }
}


void mitk::SplineVtkMapper3D::GenerateData( mitk::BaseRenderer* renderer )
{
  bool doNotDrawPoints;
  if (dynamic_cast<mitk::BoolProperty *>(this->GetDataTreeNode()->GetProperty("dontdrawpoints").GetPointer()) == NULL)
    doNotDrawPoints = true;
  else
    doNotDrawPoints = dynamic_cast<mitk::BoolProperty *>(this->GetDataTreeNode()->GetProperty("dontdrawpoints").GetPointer())->GetValue();
  
  //add or remove the PointsAssembly according to the property doNotDrawPoints
  if (!doNotDrawPoints)
  {
    Superclass::GenerateData( renderer );
    if( ! m_SplineAssembly->GetParts()->IsItemPresent(m_PointsAssembly))
      m_SplineAssembly->AddPart( m_PointsAssembly );
  }
  else
  {
    if(m_SplineAssembly->GetParts()->IsItemPresent(m_PointsAssembly))
      m_SplineAssembly->RemovePart(m_PointsAssembly);
  }
  
  if ( IsVisible( renderer ) == false )
  {
    m_SplinesActor->VisibilityOff();
    //don't care if added or not
    m_PointsAssembly->VisibilityOff();
    m_SplineAssembly->VisibilityOff();
  }
  else
  {
    m_SplinesActor->VisibilityOn();
    //don't care if added or not!
    m_PointsAssembly->VisibilityOn();
    m_SplineAssembly->VisibilityOn();
    
  }
}

bool mitk::SplineVtkMapper3D::SplinesAreAvailable()
{
  return m_SplinesAvailable;
}


vtkPolyData* mitk::SplineVtkMapper3D::GetSplinesPolyData()
{
  Mapper::Update(NULL);
  if ( m_SplinesAvailable )
    return ( dynamic_cast<vtkPolyDataMapper*>( m_SplinesActor->GetMapper() ) )->GetInput();
  else
    return vtkPolyData::New();
}

vtkActor* mitk::SplineVtkMapper3D::GetSplinesActor()
{
  Mapper::Update(NULL);
  if ( m_SplinesAvailable )
    return m_SplinesActor;
  else
    return vtkActor::New();
}

