"Resource/UI/HudMedicCharge.res"
{
	"HudMedicCharge"
	{
		"ControlName"	"EditablePanel"
		"fieldName"		"HudMedicCharge"
		"xpos"			"r160"
		"ypos"			"r70"
		"zpos"			"1"
		"wide"			"240"
		"tall"			"120"
		"visible"		"1"
		"enabled"		"1"
	}

	"Background"
	{
		"ControlName"	"ImagePanel"
		"fieldName"		"Background"
		"xpos"			"0"
		"ypos"			"0"
		"zpos"			"0"
		"wide"			"130"
		"tall"			"65"
		"visible"		"1"
		"enabled"		"1"
		"image"			"resource/tga/mask_uber_red"
		"scaleImage"	"1"			
	}
	
	"ChargeLabel"
	{
		"ControlName"	"Label"
		"fieldName"		"ChargeLabel"
		"xpos"			"30"
		"ypos"			"24"
		"zpos"			"2"
		"wide"			"110"
		"tall"			"15"
		"autoResize"	"1"
		"pinCorner"		"2"
		"visible"		"1"
		"enabled"		"1"
		"tabPosition"	"0"
		"labelText"		"#CKF3_Ubercharge"
		"textAlignment"	"west"
		"dulltext"		"0"
		"brighttext"	"0"
		"font"			"HudFontSmallest"
	}

	"IndividualChargesLabel"
	{
		"ControlName"	"Label"
		"fieldName"		"IndividualChargesLabel"
		"xpos"			"30"
		"ypos"			"24"
		"zpos"			"2"
		"wide"			"110"
		"tall"			"15"
		"autoResize"	"1"
		"pinCorner"		"2"
		"visible"		"1"
		"enabled"		"1"
		"tabPosition"	"0"
		"labelText"		"#CKF3_IndividualUbercharges"
		"textAlignment"	"west"
		"dulltext"		"0"
		"brighttext"	"0"
		"font"			"HudFontSmallest"
	}
	
	"ChargeMeter"
	{	
		"ControlName"	"ContinuousProgressBar"
		"fieldName"		"ChargeMeter"
		"font"			"Default"
		"xpos"			"30"
		"ypos"			"38"
		"zpos"			"2"
		"wide"			"86"
		"tall"			"8"				
		"autoResize"	"0"
		"pinCorner"		"0"
		"visible"		"1"
		"enabled"		"1"
		"textAlignment"	"Left"
		"dulltext"		"0"
		"brighttext"	"0"
	}		
}
