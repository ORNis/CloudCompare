//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "ccColorGradientDlg.h"

//Local
#include "ccQtHelpers.h"

//Qt
#include <QColorDialog>

//system
#include <assert.h>

//persistent parameters
QColor s_firstColor(Qt::black);
QColor s_secondColor(Qt::white);
ccColorGradientDlg::GradientType s_lastType(ccColorGradientDlg::Default);
static int s_lastFreq = 5;

ccColorGradientDlg::ccColorGradientDlg(QWidget* parent)
	: QDialog(parent)
	, Ui::ColorGradientDialog()
{
	setupUi(this);

	setWindowFlags(Qt::Tool/*Qt::Dialog | Qt::WindowStaysOnTopHint*/);

	connect(firstColorButton, SIGNAL(clicked()), this, SLOT(changeFirstColor()));
	connect(secondColorButton, SIGNAL(clicked()), this, SLOT(changeSecondColor()));

	//restore previous parameters
	ccQtHelpers::SetButtonColor(secondColorButton,s_secondColor);
	ccQtHelpers::SetButtonColor(firstColorButton,s_firstColor);
	setType(s_lastType);
	bandingFreqSpinBox->setValue(s_lastFreq);
}

unsigned char ccColorGradientDlg::getDimension() const
{
	return static_cast<unsigned char>(directionComboBox->currentIndex());
}

void ccColorGradientDlg::setType(ccColorGradientDlg::GradientType type)
{
	switch(type)
	{
	case Default:
		defaultRampRadioButton->setChecked(true);
		break;
	case TwoColors:
		customRampRadioButton->setChecked(true);
		break;
	case Banding:
		bandingRadioButton->setChecked(true);
		break;
	default:
		assert(false);
	}
}

ccColorGradientDlg::GradientType ccColorGradientDlg::getType() const
{
	//ugly hack: we use 's_lastType' here as the type is only requested
	//when the dialog is accepted
	if (customRampRadioButton->isChecked())
		s_lastType = TwoColors;
	else if (bandingRadioButton->isChecked())
		s_lastType = Banding;
	else
		s_lastType = Default;

	return s_lastType;
}

void ccColorGradientDlg::getColors(QColor& first, QColor& second) const
{
	assert(customRampRadioButton->isChecked());
	first = s_firstColor;
	second = s_secondColor;
}

int ccColorGradientDlg::getBandingFrequency() const
{
	assert(bandingRadioButton->isChecked());
	//ugly hack: we use 's_lastFreq' here as the frequency is only requested
	//when the dialog is accepted
	s_lastFreq = bandingFreqSpinBox->value();
	return s_lastFreq;
}

void ccColorGradientDlg::changeFirstColor()
{
	QColor newCol = QColorDialog::getColor(s_firstColor, this);
	if (newCol.isValid())
	{
		s_firstColor = newCol;
		ccQtHelpers::SetButtonColor(firstColorButton,s_firstColor);
	}
}

void ccColorGradientDlg::changeSecondColor()
{
	QColor newCol = QColorDialog::getColor(s_secondColor, this);
	if (newCol.isValid())
	{
		s_secondColor = newCol;
		ccQtHelpers::SetButtonColor(secondColorButton,s_secondColor);
	}
}
