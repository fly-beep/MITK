/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

#include "QmitkSegmentationOrganNamesHandling.h"

#include <mitkAnatomicalStructureColorPresets.h>
#include <vtkSmartPointer.h>

namespace mitk
{
  namespace OrganNamesHandling
  {
    QStringList GetDefaultOrganColorString()
    {
      QStringList organColors;

      auto presets = vtkSmartPointer<AnatomicalStructureColorPresets>::New();
      presets->LoadPreset();

      for (const auto& preset : presets->GetColorPresets())
      {
        auto organName = preset.first.c_str();
        auto color = QColor(preset.second.GetRed(), preset.second.GetGreen(), preset.second.GetBlue());

        AppendToOrganList(organColors, organName, color);
      }
      return organColors;
    }

    void UpdateOrganList(QStringList& organColors, const QString& organname, mitk::Color color)
    {
      QString listElement(organname + QColor(color.GetRed() * 255, color.GetGreen() * 255, color.GetBlue() * 255).name());

      // remove previous definition if necessary
      int oldIndex = organColors.indexOf(QRegExp(QRegExp::escape(organname) + "#......", Qt::CaseInsensitive));
      if (oldIndex < 0 || organColors.at(oldIndex) != listElement)
      {
        if (oldIndex >= 0)
        {
          organColors.removeAt(oldIndex);
        }

        // add colored organ name AND sort list
        organColors.append(listElement);
        organColors.sort();
      }
    }

    void AppendToOrganList(QStringList& organColors, const QString& organname, const QColor& color)
    {
      organColors.append(organname + color.name());
    }
  }
}
