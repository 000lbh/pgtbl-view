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
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidgetItem>
#include <cstdint>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void initialize();
	void refresh();
	void onItemExpanded(QTreeWidgetItem *item);
	void onItemCollapsed(QTreeWidgetItem *item);
	void onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);

private:
	enum { ROLE_LEVEL = Qt::UserRole, ROLE_INDEX, ROLE_RAW, ROLE_PATH };
	enum { LEVEL_CR3 = 0, LEVEL_PML4 = 1, LEVEL_PDPT = 2,
	       LEVEL_PD = 3, LEVEL_PT = 4 };

	bool loadTable(QTreeWidgetItem *parent);
	bool isEntryExpandable(int level, uint64_t raw) const;
	void displayEntry(uint64_t raw, int level);
	int  computeLevel(QTreeWidgetItem *item) const;
	void computePath(QTreeWidgetItem *item, int *l1, int *l2, int *l3) const;
	QString levelName(int level) const;
	QString flagsText(uint64_t raw, int level) const;

	Ui::MainWindow *ui;
	int fd;
	uint64_t key;
};

#endif
