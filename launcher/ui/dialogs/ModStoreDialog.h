// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Prism Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDialog>

namespace Ui {
class ModStoreDialog;
}

class ModStore;

class ModStoreDialog : public QDialog {
    Q_OBJECT

   public:
    explicit ModStoreDialog(QWidget* parent = nullptr);
    ~ModStoreDialog() override;

   private slots:
    void on_btnConsolidate_clicked();
    void on_btnDryRun_clicked();
    void on_btnGC_clicked();
    void on_btnOpenFolder_clicked();

   private:
    void refreshStats();
    void runConsolidate(bool dryRun);

    Ui::ModStoreDialog* ui;
    ModStore* m_store;
};
