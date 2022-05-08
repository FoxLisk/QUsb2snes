/*
 * Copyright (c) 2018 Sylvain "Skarsnik" Colinet.
 *
 * This file is part of the QUsb2Snes project.
 * (see https://github.com/Skarsnik/QUsb2snes).
 *
 * QUsb2Snes is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * QUsb2Snes is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QUsb2Snes.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "tempdeviceselector.h"
#include "ui_tempdeviceselector.h"
#include <QMessageBox>

TempDeviceSelector::TempDeviceSelector(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TempDeviceSelector)
{
    ui->setupUi(this);
}

TempDeviceSelector::~TempDeviceSelector()
{
    delete ui;
}

void TempDeviceSelector::accept()
{
    if (ui->sd2snesRadioButton->isChecked())
        devices.append("SD2SNES");
    if (ui->luaRadioButton->isChecked())
        devices.append("LUA");
    if (ui->classicRradioButton->isChecked())
        devices.append("CLASSIC");
    if (ui->retroarchRadioButton->isChecked())
        devices.append("RETROARCH");
    if (ui->nwaRadioButton->isChecked())
        devices.append("NWA");
    if (devices.isEmpty())
    {
        QMessageBox::warning(this, "No device selected", "You need to select a device to continue");
        return ;
    }
    QDialog::accept();
}
