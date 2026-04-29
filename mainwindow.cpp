/*
 * pgtbl-view — Page Table Viewer
 * Copyright (C) 2025 Bohai Li <lbhlbhlbh2002@icloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFile>
#include <QMessageBox>
#include <QHeaderView>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cstring>

#include "pgtbl_ioctl.h"

static constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
static constexpr uint64_t PAGE_RW      = 1ULL << 1;
static constexpr uint64_t PAGE_USER    = 1ULL << 2;
static constexpr uint64_t PAGE_PWT     = 1ULL << 3;
static constexpr uint64_t PAGE_PCD     = 1ULL << 4;
static constexpr uint64_t PAGE_ACCESSED = 1ULL << 5;
static constexpr uint64_t PAGE_DIRTY   = 1ULL << 6;
static constexpr uint64_t PAGE_PSE     = 1ULL << 7;
static constexpr uint64_t PAGE_GLOBAL  = 1ULL << 8;
static constexpr uint64_t PAGE_NX      = 1ULL << 63;

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, ui(new Ui::MainWindow)
	, fd(-1)
	, key(0)
{
	ui->setupUi(this);

	connect(ui->actionInitialize, &QAction::triggered,
		this, &MainWindow::initialize);
	connect(ui->actionQuit, &QAction::triggered,
		this, &MainWindow::close);
	connect(ui->initButton, &QPushButton::clicked,
		this, &MainWindow::initialize);
	connect(ui->refreshButton, &QPushButton::clicked,
		this, &MainWindow::refresh);

	connect(ui->pageTree, &QTreeWidget::itemExpanded,
		this, &MainWindow::onItemExpanded);
	connect(ui->pageTree, &QTreeWidget::itemCollapsed,
		this, &MainWindow::onItemCollapsed);
	connect(ui->pageTree, &QTreeWidget::currentItemChanged,
		this, &MainWindow::onCurrentItemChanged);

	ui->pageTree->setIndentation(18);
	ui->pageTree->header()->setStretchLastSection(true);
	ui->pageTree->setColumnWidth(0, 140);
}

MainWindow::~MainWindow()
{
	if (fd >= 0)
		::close(fd);
	delete ui;
}

void MainWindow::initialize()
{
	if (fd >= 0) {
		::close(fd);
		fd = -1;
	}

	fd = open("/dev/pgtbl-view", O_RDWR);
	if (fd < 0) {
		QMessageBox::warning(this, "Error",
			"Failed to open /dev/pgtbl-view.\n"
			"Make sure the pgtbl kernel module is loaded.");
		ui->statusbar->showMessage("Device open failed");
		return;
	}

	int keyfd = open("/dev/pgtbl-key", O_RDONLY);
	if (keyfd >= 0) {
		ssize_t n = read(keyfd, &key, sizeof(key));
		::close(keyfd);
		if (n == sizeof(key)) {
			ui->keyEdit->setText(
				QString::number(key, 16)
					.rightJustified(16, '0'));
			ui->statusbar->showMessage(
				"Key auto-read from /dev/pgtbl-key");
		} else {
			key = 0;
			ui->statusbar->showMessage(
				"Key read failed, enter manually");
		}
	} else {
		ui->statusbar->showMessage(
			"Cannot read /dev/pgtbl-key (not root?), "
			"enter key manually");
	}

	ui->refreshButton->setEnabled(true);
	ui->detailGroup->setEnabled(true);

	if (key != 0) {
		refresh();
	} else {
		ui->statusbar->showMessage(
			"Enter key manually and click Refresh");
	}
}

void MainWindow::refresh()
{
	bool ok;
	QString keyText = ui->keyEdit->text();
	if (!keyText.isEmpty()) {
		key = keyText.toULongLong(&ok, 16);
		if (!ok) {
			ui->statusbar->showMessage("Invalid key format");
			return;
		}
	}

	if (fd < 0 || key == 0) {
		ui->statusbar->showMessage("Device not initialized");
		return;
	}

	ui->pageTree->clear();

	QTreeWidgetItem *cr3 = new QTreeWidgetItem;
	cr3->setText(0, QStringLiteral("CR3"));
	cr3->setData(0, ROLE_LEVEL, LEVEL_CR3);
	cr3->setData(0, ROLE_INDEX, QVariant(-1));
	ui->pageTree->addTopLevelItem(cr3);

	QTreeWidgetItem *dummy = new QTreeWidgetItem(cr3);
	dummy->setData(0, ROLE_INDEX, QVariant(-2));

	cr3->setExpanded(true);

	ui->pageTree->setCurrentItem(cr3);
	ui->statusbar->showMessage("Page table loaded");
}

void MainWindow::onItemExpanded(QTreeWidgetItem *item)
{
	int lv = item->data(0, ROLE_LEVEL).toInt();

	while (item->childCount() > 0)
		delete item->takeChild(0);

	if (loadTable(item))
		return;

	QTreeWidgetItem *dummy = new QTreeWidgetItem(item);
	dummy->setData(0, ROLE_INDEX, QVariant(-2));

	QString msg;
	if (lv == LEVEL_PT)
		msg = "Page table entries are terminal — "
		      "cannot expand further";
	else
		msg = "Page table has been updated, "
		      "cannot expand further";
	ui->statusbar->showMessage(msg);
}

void MainWindow::onItemCollapsed(QTreeWidgetItem *item)
{
	while (item->childCount() > 0)
		delete item->takeChild(0);

	QTreeWidgetItem *dummy = new QTreeWidgetItem(item);
	dummy->setData(0, ROLE_INDEX, QVariant(-2));
}

void MainWindow::onCurrentItemChanged(QTreeWidgetItem *current,
				      QTreeWidgetItem * /*previous*/)
{
	if (!current)
		return;

	int level = current->data(0, ROLE_LEVEL).toInt();

	if (level < 0) {
		ui->hintLabel->clear();
		return;
	}

	uint64_t raw =
		current->data(0, ROLE_RAW).toULongLong();
	displayEntry(raw, level);

	bool expandable = isEntryExpandable(level, raw);
	if (!expandable) {
		if (level == LEVEL_PT) {
			ui->hintLabel->setText(
				"Terminal entry (PTE) — "
				"cannot expand further");
		} else if (!(raw & PAGE_PRESENT)) {
			ui->hintLabel->setText(
				"Entry is not present — "
				"cannot expand");
		} else if (raw & PAGE_PSE) {
			if (level == LEVEL_PDPT)
				ui->hintLabel->setText(
					"Large page (1 GB) at PDPT — "
					"cannot expand further");
			else
				ui->hintLabel->setText(
					"Large page (2 MB) at PD — "
					"cannot expand further");
		} else {
			ui->hintLabel->setText(
				"Cannot expand this entry");
		}
	} else {
		if (level == LEVEL_CR3)
			ui->hintLabel->setText(
				"CR3 — contains PML4 table");
		else
			ui->hintLabel->clear();
	}
}

bool MainWindow::loadTable(QTreeWidgetItem *parent)
{
	int l1, l2, l3;
	computePath(parent, &l1, &l2, &l3);

	struct pgtbl_table tbl;
	memset(&tbl, 0, sizeof(tbl));
	tbl.key = key;
	tbl.l1 = static_cast<__s32>(l1);
	tbl.l2 = static_cast<__s32>(l2);
	tbl.l3 = static_cast<__s32>(l3);

	int ret = ioctl(fd, PGTBL_IOC_QUERY_TABLE, &tbl);
	if (ret < 0)
		return false;

	parent->setData(0, ROLE_RAW,
			QVariant(static_cast<quint64>(tbl.parent_entry)));

	int parentLevel = parent->data(0, ROLE_LEVEL).toInt();
	if (parentLevel == LEVEL_CR3) {
		parent->setText(1,
			QString("0x")
			+ QString::number(tbl.parent_entry, 16)
				.rightJustified(16, '0'));
	}

	int childLevel = parentLevel + 1;
	for (int i = 0; i < PGTBL_NENTRIES; i++) {
		uint64_t raw = tbl.entries[i];

		QTreeWidgetItem *child = new QTreeWidgetItem;
		child->setText(0, QString("#%1").arg(i));
		child->setText(1, flagsText(raw, childLevel));
		child->setData(0, ROLE_LEVEL, childLevel);
		child->setData(0, ROLE_INDEX, i);
		child->setData(0, ROLE_RAW,
			       QVariant(static_cast<quint64>(raw)));

		if (isEntryExpandable(childLevel, raw)) {
			QTreeWidgetItem *dummy =
				new QTreeWidgetItem(child);
			dummy->setData(0, ROLE_INDEX, QVariant(-2));
		}

		parent->addChild(child);
	}

	return true;
}

bool MainWindow::isEntryExpandable(int level, uint64_t raw) const
{
	switch (level) {
	case LEVEL_CR3:
		return true;
	case LEVEL_PML4:
		return (raw & PAGE_PRESENT) != 0;
	case LEVEL_PDPT:
		return (raw & PAGE_PRESENT) != 0
		       && (raw & PAGE_PSE) == 0;
	case LEVEL_PD:
		return (raw & PAGE_PRESENT) != 0
		       && (raw & PAGE_PSE) == 0;
	default:
		return false;
	}
}

void MainWindow::displayEntry(uint64_t raw, int level)
{
	ui->rawValueEdit->setText(
		QString("0x")
		+ QString::number(raw, 16).rightJustified(16, '0'));

	ui->levelEdit->setText(levelName(level));

	uint64_t phys = raw & 0x000FFFFFFFFFF000ULL;
	if (level == LEVEL_CR3)
		phys = raw & 0x000FFFFFFFFFF000ULL;
	ui->physAddrEdit->setText(
		QString("0x")
		+ QString::number(phys, 16).rightJustified(16, '0'));

	if (level == LEVEL_CR3) {
		ui->presentCheck->setChecked(false);
		ui->rwCheck->setChecked(false);
		ui->userCheck->setChecked(false);
		ui->pwtCheck->setChecked(false);
		ui->pcdCheck->setChecked(false);
		ui->accessedCheck->setChecked(false);
		ui->dirtyCheck->setChecked(false);
		ui->pseCheck->setChecked(false);
		ui->globalCheck->setChecked(false);
		ui->nxCheck->setChecked(false);
		return;
	}

	bool present  = (raw & PAGE_PRESENT)  != 0;
	bool rw       = (raw & PAGE_RW)       != 0;
	bool user     = (raw & PAGE_USER)     != 0;
	bool pwt      = (raw & PAGE_PWT)      != 0;
	bool pcd      = (raw & PAGE_PCD)      != 0;
	bool accessed = (raw & PAGE_ACCESSED) != 0;
	bool dirty    = (raw & PAGE_DIRTY)    != 0;
	bool pse      = (raw & PAGE_PSE)      != 0;
	bool global   = (raw & PAGE_GLOBAL)   != 0;
	bool nx       = (raw & PAGE_NX)       != 0;

	ui->presentCheck->setChecked(present);
	ui->rwCheck->setChecked(rw);
	ui->userCheck->setChecked(user);
	ui->pwtCheck->setChecked(pwt);
	ui->pcdCheck->setChecked(pcd);
	ui->accessedCheck->setChecked(accessed);
	ui->globalCheck->setChecked(global);
	ui->nxCheck->setChecked(nx);

	bool showPse = (level == LEVEL_PDPT || level == LEVEL_PD);
	ui->pseCheck->setVisible(showPse);
	if (showPse)
		ui->pseCheck->setChecked(pse);
	else
		ui->pseCheck->setChecked(false);

	bool showDirty =
		(level == LEVEL_PDPT && pse) ||
		(level == LEVEL_PD && pse) ||
		level == LEVEL_PT;
	if (!showDirty)
		ui->dirtyCheck->setChecked(false);
	else
		ui->dirtyCheck->setChecked(dirty);
}

void MainWindow::computePath(QTreeWidgetItem *item,
			     int *l1, int *l2, int *l3) const
{
	*l1 = *l2 = *l3 = -1;
	QVector<int> indices;

	QTreeWidgetItem *p = item;
	while (p) {
		int lv = p->data(0, ROLE_LEVEL).toInt();
		int ix = p->data(0, ROLE_INDEX).toInt();
		if (lv > 0 && ix >= 0)
			indices.prepend(ix);
		p = p->parent();
	}

	if (indices.size() >= 1) *l1 = indices[0];
	if (indices.size() >= 2) *l2 = indices[1];
	if (indices.size() >= 3) *l3 = indices[2];
}

QString MainWindow::levelName(int level) const
{
	switch (level) {
	case LEVEL_CR3:  return QStringLiteral("CR3");
	case LEVEL_PML4: return QStringLiteral("PML4E");
	case LEVEL_PDPT: return QStringLiteral("PDPTE");
	case LEVEL_PD:   return QStringLiteral("PDE");
	case LEVEL_PT:   return QStringLiteral("PTE");
	default:         return QStringLiteral("???");
	}
}

QString MainWindow::flagsText(uint64_t raw, int level) const
{
	if (level == LEVEL_CR3)
		return QString();

	if (!(raw & PAGE_PRESENT))
		return QStringLiteral("-- (not present)");

	QStringList parts;
	parts.append(raw & PAGE_PRESENT  ? "P"  : "--");
	parts.append(raw & PAGE_RW       ? "RW" : "RO");
	parts.append(raw & PAGE_USER     ? "US" : "S");
	parts.append(raw & PAGE_PWT      ? "WT" : "--");
	parts.append(raw & PAGE_PCD      ? "CD" : "--");
	parts.append(raw & PAGE_ACCESSED ? "A"  : "--");

	if (level == LEVEL_PML4) {
		parts.append("--");
	} else {
		parts.append(raw & PAGE_DIRTY ? "D" : "--");
	}

	bool showPse = (level == LEVEL_PDPT || level == LEVEL_PD);
	if (showPse) {
		if (raw & PAGE_PSE) {
			parts.append("PS");
		} else {
			parts.append("--");
		}
	} else {
		parts.append("--");
	}

	parts.append(raw & PAGE_GLOBAL ? "G" : "--");
	parts.append(raw & PAGE_NX ? "NX" : "--");

	QString s = parts.join('|');

	if (showPse && (raw & PAGE_PSE)) {
		if (level == LEVEL_PDPT)
			s += " (1GB page)";
		else
			s += " (2MB page)";
	}

	return s;
}
