/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

// render window manager UI module
#include "QmitkRenderWindowContextDataStorageInspector.h"

#include <QmitkCustomVariants.h>
#include <QmitkEnums.h>

// qt
#include <QMenu>
#include <QSignalMapper>

QmitkRenderWindowContextDataStorageInspector::QmitkRenderWindowContextDataStorageInspector(
  QWidget* parent /* =nullptr */,
  mitk::BaseRenderer* renderer /* = nullptr */)
  : QmitkAbstractDataStorageInspector(parent)
{
  m_Controls.setupUi(this);

  mitk::RenderWindowLayerUtilities::RendererVector controlledRenderer{ renderer };

  // initialize the render window layer controller and the render window view direction controller
  m_RenderWindowLayerController = std::make_unique<mitk::RenderWindowLayerController>();
  m_RenderWindowLayerController->SetControlledRenderer(controlledRenderer);

  m_StorageModel = std::make_unique<QmitkRenderWindowDataStorageTreeModel>(this);
  m_StorageModel->SetControlledRenderer(controlledRenderer);

  m_Controls.renderWindowTreeView->setModel(m_StorageModel.get());
  m_Controls.renderWindowTreeView->setHeaderHidden(true);
  m_Controls.renderWindowTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_Controls.renderWindowTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_Controls.renderWindowTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_Controls.renderWindowTreeView->setAlternatingRowColors(true);
  m_Controls.renderWindowTreeView->setDragEnabled(true);
  m_Controls.renderWindowTreeView->setDropIndicatorShown(true);
  m_Controls.renderWindowTreeView->setAcceptDrops(true);
  m_Controls.renderWindowTreeView->setContextMenuPolicy(Qt::CustomContextMenu);

  connect(m_Controls.renderWindowTreeView, &QTreeView::customContextMenuRequested,
          this, &QmitkRenderWindowContextDataStorageInspector::OnContextMenuRequested);

  SetUpConnections();

  if (nullptr == renderer)
  {
    return;
  }

  m_StorageModel->SetCurrentRenderer(renderer);
}

QAbstractItemView* QmitkRenderWindowContextDataStorageInspector::GetView()
{
  return m_Controls.renderWindowTreeView;
}

const QAbstractItemView* QmitkRenderWindowContextDataStorageInspector::GetView() const
{
  return m_Controls.renderWindowTreeView;
}

void QmitkRenderWindowContextDataStorageInspector::SetSelectionMode(SelectionMode mode)
{
  m_Controls.renderWindowTreeView->setSelectionMode(mode);
}

QmitkRenderWindowContextDataStorageInspector::SelectionMode QmitkRenderWindowContextDataStorageInspector::GetSelectionMode() const
{
  return m_Controls.renderWindowTreeView->selectionMode();
}

QItemSelectionModel* QmitkRenderWindowContextDataStorageInspector::GetDataNodeSelectionModel() const
{
  return m_Controls.renderWindowTreeView->selectionModel();
}

void QmitkRenderWindowContextDataStorageInspector::Initialize()
{
  auto dataStorage = m_DataStorage.Lock();

  if (dataStorage.IsNull())
    return;

  m_StorageModel->SetDataStorage(dataStorage);
  m_StorageModel->SetNodePredicate(m_NodePredicate);

  m_RenderWindowLayerController->SetDataStorage(dataStorage);

  m_Connector->SetView(m_Controls.renderWindowTreeView);
}

void QmitkRenderWindowContextDataStorageInspector::SetUpConnections()
{
  connect(m_StorageModel.get(), &QAbstractItemModel::rowsInserted, this, &QmitkRenderWindowContextDataStorageInspector::ModelRowsInserted);
}

void QmitkRenderWindowContextDataStorageInspector::ModelRowsInserted(const QModelIndex& parent, int /*start*/, int /*end*/)
{
  m_Controls.renderWindowTreeView->setExpanded(parent, true);
}

void QmitkRenderWindowContextDataStorageInspector::ResetRenderer()
{
  m_RenderWindowLayerController->ResetRenderer(true, m_StorageModel->GetCurrentRenderer());
  m_Controls.renderWindowTreeView->clearSelection();
}

void QmitkRenderWindowContextDataStorageInspector::OnContextMenuRequested(const QPoint& pos)
{
  QMenu contextMenu;
  contextMenu.addAction(tr("Reinit with node"), this, &QmitkRenderWindowContextDataStorageInspector::OnReinit);
  contextMenu.addAction(tr("Reset to node geometry"), this, &QmitkRenderWindowContextDataStorageInspector::OnReset);

  contextMenu.exec(this->mapToGlobal(pos));
}

void QmitkRenderWindowContextDataStorageInspector::OnReinit()
{
  auto nodes = this->GetSelectedNodes();
  emit ReinitAction(nodes);
}

void QmitkRenderWindowContextDataStorageInspector::OnReset()
{
  auto nodes = this->GetSelectedNodes();
  emit ResetAction(nodes);
}

QList<mitk::DataNode::Pointer> QmitkRenderWindowContextDataStorageInspector::GetSelectedNodes()
{
  auto baseRenderer = m_StorageModel->GetCurrentRenderer();

  QList<mitk::DataNode::Pointer> nodes;
  QModelIndexList selectedIndexes = this->GetDataNodeSelectionModel()->selectedIndexes();
  for (const auto& index : qAsConst(selectedIndexes))
  {
    QVariant qvariantDataNode = m_StorageModel->data(index, QmitkDataNodeRole);
    if (qvariantDataNode.canConvert<mitk::DataNode::Pointer>())
    {
      mitk::DataNode* dataNode = qvariantDataNode.value<mitk::DataNode::Pointer>();
      nodes.insert(nodes.size(), dataNode);
    }
  }

  return nodes;
}
