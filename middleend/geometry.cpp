#include "geometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace templide::geometry {
    const std::map<std::string, std::array<Site, 4>>& connection_sites() {
        static const std::map<std::string, std::array<Site, 4>> table = {
            {"accentBorderCallout1", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"accentBorderCallout2", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"accentBorderCallout3", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"accentCallout1", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"accentCallout2", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"accentCallout3", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonBackPrevious", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonBeginning", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonBlank", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonDocument", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonEnd", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonForwardNext", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonHelp", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonHome", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonInformation", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonMovie", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonReturn", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"actionButtonSound", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"arc", {{{0, 0, 0}, {2, 1, 1}, {1, 0, 1}, {0, 0, 0}}}},
            {"bentArrow", {{{0, 0.8167, 0}, {1, 0.8167, 0.5}, {2, 0.0917, 1}, {2, 0.0917, 1}}}},
            {"bentUpArrow", {{{1, 0.6333, 0.25}, {4, 0.9083, 0.625}, {3, 0.4542, 1}, {2, 0, 0.875}}}},
            {"bevel", {{{6, 0.5, 0}, {0, 1, 0.5}, {2, 0.5, 1}, {4, 0, 0.5}}}},
            {"blockArc", {{{2, 0.5, 0.5}, {1, 0.9083, 0.5}, {2, 0.5, 0.5}, {0, 0.0917, 0.5}}}},
            {"borderCallout1", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"borderCallout2", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"borderCallout3", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"bracePair", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"bracketPair", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"callout1", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"callout2", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"callout3", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"can", {{{1, 0.5, 0}, {4, 1, 0.5}, {3, 0.5, 1}, {2, 0, 0.5}}}},
            {"chevron", {{{0, 0.3167, 0}, {3, 1, 0.5}, {2, 0.3167, 1}, {1, 0.3667, 0.5}}}},
            {"chord", {{{1, 0.5, 0}, {2, 0.6478, 0.4516}, {0, 0.7957, 0.9032}, {2, 0.6478, 0.4516}}}},
            {"circularArrow", {{{3, 0.7717, 0.329}, {2, 0.9083, 0.5}, {0, 0.0917, 0.5}, {0, 0.0917, 0.5}}}},
            {"cloud", {{{3, 0.5, 0.0572}, {0, 0.9992, 0.5}, {1, 0.5, 0.9989}, {2, 0.0031, 0.5}}}},
            {"cloudCallout", {{{3, 0.5, 0.0572}, {2, 0.9992, 0.5}, {1, 0.5, 0.9989}, {0, 0.0031, 0.5}}}},
            {"corner", {{{3, 0.1833, 0}, {0, 1, 0.75}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"cornerTabs", {{{5, 0.938, 0}, {9, 1, 0.0846}, {7, 0.938, 1}, {1, 0, 0.0846}}}},
            {"cube", {{{0, 0.5917, 0}, {5, 1, 0.375}, {3, 0.4083, 1}, {2, 0, 0.625}}}},
            {"curvedDownArrow", {{{0, 0.4542, 0}, {4, 0.9885, 0.75}, {2, 0.6218, 0.75}, {1, 0.0917, 1}}}},
            {"curvedLeftArrow", {{{0, 0, 0.125}, {4, 1, 0.4375}, {3, 0.1833, 0.9947}, {1, 0.1833, 0.4947}}}},
            {"curvedRightArrow", {{{4, 1, 0.125}, {3, 0.8167, 0.4947}, {1, 0.8167, 0.9947}, {0, 0, 0.4375}}}},
            {"curvedUpArrow", {{{1, 0.6218, 0.25}, {4, 0.9885, 0.25}, {3, 0.4542, 1}, {2, 0.0917, 0}}}},
            {"decagon", {{{9, 0.6545, 0}, {1, 1, 0.5}, {3, 0.6545, 1}, {6, 0, 0.5}}}},
            {"diagStripe", {{{3, 0.75, 0}, {0, 0.5, 0.5}, {0, 0.5, 0.5}, {1, 0, 0.75}}}},
            {"diamond", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"dodecagon", {{{10, 0.366, 0}, {1, 1, 0.366}, {4, 0.634, 1}, {7, 0, 0.634}}}},
            {"donut", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"doubleWave", {{{0, 0.5, 0.0625}, {3, 1, 0.5}, {2, 0.5, 0.9375}, {1, 0, 0.5}}}},
            {"downArrow", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"downArrowCallout", {{{0, 0.5, 0}, {3, 1, 0.3249}, {2, 0.5, 1}, {1, 0, 0.3249}}}},
            {"ellipse", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"ellipseRibbon", {{{0, 0.5, 0.25}, {3, 0.875, 0.4297}, {2, 0.5, 1}, {1, 0.125, 0.4297}}}},
            {"ellipseRibbon2", {{{0, 0.5, 0}, {3, 0.875, 0.5703}, {2, 0.5, 0.75}, {1, 0.125, 0.5703}}}},
            {"flowChartAlternateProcess", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartCollate", {{{0, 0.5, 0}, {1, 0.5, 0.5}, {2, 0.5, 1}, {1, 0.5, 0.5}}}},
            {"flowChartConnector", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"flowChartDecision", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartDelay", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartDisplay", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartDocument", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 0.9339}, {1, 0, 0.5}}}},
            {"flowChartExtract", {{{0, 0.5, 0}, {3, 0.75, 0.5}, {2, 0.5, 1}, {1, 0.25, 0.5}}}},
            {"flowChartInputOutput", {{{1, 0.5, 0}, {5, 0.9, 0.5}, {4, 0.5, 1}, {2, 0.1, 0.5}}}},
            {"flowChartInternalStorage", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartMagneticDisk", {{{1, 0.5, 0}, {4, 1, 0.5}, {3, 0.5, 1}, {2, 0, 0.5}}}},
            {"flowChartMagneticDrum", {{{0, 0.5, 0}, {4, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartMagneticTape", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartManualInput", {{{0, 0.5, 0.1}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartManualOperation", {{{0, 0.5, 0}, {3, 0.9, 0.5}, {2, 0.5, 1}, {1, 0.1, 0.5}}}},
            {"flowChartMerge", {{{0, 0.5, 0}, {3, 0.75, 0.5}, {2, 0.5, 1}, {1, 0.25, 0.5}}}},
            {"flowChartMultidocument", {{{0, 0.5688, 0}, {3, 1, 0.5}, {2, 0.4305, 0.9621}, {1, 0, 0.5}}}},
            {"flowChartOfflineStorage", {{{3, 0.5, 0}, {0, 0.75, 0.5}, {1, 0.5, 1}, {2, 0.25, 0.5}}}},
            {"flowChartOffpageConnector", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartOnlineStorage", {{{0, 0.5, 0}, {3, 0.8333, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartOr", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"flowChartPredefinedProcess", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartPreparation", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartProcess", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartPunchedCard", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartPunchedTape", {{{0, 0.5, 0.1}, {3, 1, 0.5}, {2, 0.5, 0.9}, {1, 0, 0.5}}}},
            {"flowChartSort", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"flowChartSummingJunction", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"flowChartTerminator", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"foldedCorner", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"frame", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"gear6", {{{5, 0.5, 0.0116}, {0, 0.9092, 0.2319}, {2, 0.5, 0.9884}, {3, 0, 0.7681}}}},
            {"gear9", {{{8, 0.5, 0.0043}, {1, 0.9867, 0.4064}, {3, 0.6622, 0.9689}, {6, 0, 0.4064}}}},
            {"halfFrame", {{{3, 0.5, 0}, {0, 0.8333, 0.1667}, {1, 0.1222, 0.8778}, {2, 0, 0.5}}}},
            {"heart", {{{0, 0.5, 0.25}, {0, 0.5, 0.25}, {1, 0.5, 1}, {0, 0.5, 0.25}}}},
            {"heptagon", {{{6, 0.5, 0}, {1, 1, 0.6431}, {3, 0.2775, 1}, {4, 0, 0.6431}}}},
            {"hexagon", {{{4, 0.1833, 0}, {0, 1, 0.5}, {1, 0.8167, 1}, {3, 0, 0.5}}}},
            {"homePlate", {{{0, 0.3167, 0}, {3, 1, 0.5}, {2, 0.3167, 1}, {1, 0, 0.5}}}},
            {"horizontalScroll", {{{0, 0.5, 0.125}, {3, 1, 0.5}, {2, 0.5, 0.875}, {1, 0, 0.5}}}},
            {"irregularSeal1", {{{0, 0.6723, 0}, {3, 1, 0.6153}, {2, 0.3928, 1}, {1, 0, 0.3988}}}},
            {"irregularSeal2", {{{0, 0.4501, 0.0874}, {3, 1, 0.3076}, {2, 0.5376, 0.8723}, {1, 0, 0.5962}}}},
            {"leftArrow", {{{0, 0.3667, 0}, {3, 1, 0.5}, {2, 0.3667, 1}, {1, 0, 0.5}}}},
            {"leftArrowCallout", {{{0, 0.6751, 0}, {3, 1, 0.5}, {2, 0.6751, 1}, {1, 0, 0.5}}}},
            {"leftBrace", {{{0, 1, 0}, {0, 1, 0}, {2, 1, 1}, {1, 0, 0.5}}}},
            {"leftBracket", {{{0, 1, 0}, {0, 1, 0}, {2, 1, 1}, {1, 0, 0.5}}}},
            {"leftCircularArrow", {{{0, 0.0917, 0.5}, {2, 0.9083, 0.5}, {3, 0.7717, 0.671}, {0, 0.0917, 0.5}}}},
            {"leftRightArrow", {{{0, 0.6333, 0}, {7, 1, 0.5}, {4, 0.3667, 1}, {3, 0, 0.5}}}},
            {"leftRightArrowCallout", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"leftRightCircularArrow", {{{5, 0.2283, 0.329}, {3, 0.9083, 0.5}, {0, 0.0917, 0.5}, {0, 0.0917, 0.5}}}},
            {"leftRightRibbon", {{{4, 0.3667, 0}, {0, 1, 0.5833}, {1, 0.6333, 1}, {3, 0, 0.4167}}}},
            {"leftRightUpArrow", {{{0, 0.5, 0}, {3, 1, 0.75}, {2, 0.5, 0.875}, {1, 0, 0.75}}}},
            {"leftUpArrow", {{{1, 0.6333, 0.25}, {6, 0.9083, 0.5625}, {5, 0.5458, 0.875}, {2, 0.1833, 0.5}}}},
            {"lightningBolt", {{{0, 0.3922, 0}, {5, 0.7675, 0.5559}, {3, 0.4635, 0.6905}, {2, 0.2325, 0.4493}}}},
            {"line", {{{0, 0, 0}, {1, 1, 1}, {1, 1, 1}, {0, 0, 0}}}},
            {"lineInv", {{{1, 1, 0}, {1, 1, 0}, {0, 0, 1}, {0, 0, 1}}}},
            {"mathDivide", {{{3, 0.5, 0.1179}, {0, 0.8674, 0.5}, {1, 0.5, 0.8821}, {2, 0.1326, 0.5}}}},
            {"mathEqual", {{{5, 0.5, 0.206}, {0, 0.8675, 0.3236}, {2, 0.5, 0.794}, {3, 0.1326, 0.3236}}}},
            {"mathMinus", {{{3, 0.5, 0.3824}, {0, 0.8675, 0.5}, {1, 0.5, 0.6176}, {2, 0.1325, 0.5}}}},
            {"mathMultiply", {{{0, 0.2402, 0.2402}, {1, 0.7598, 0.2402}, {2, 0.7598, 0.7598}, {0, 0.2402, 0.2402}}}},
            {"mathNotEqual", {{{5, 0.6227, 0.0402}, {0, 0.8674, 0.3236}, {2, 0.3773, 0.9598}, {3, 0.1326, 0.3236}}}},
            {"mathPlus", {{{3, 0.5, 0.1325}, {0, 0.8674, 0.5}, {1, 0.5, 0.8675}, {2, 0.1326, 0.5}}}},
            {"moon", {{{0, 1, 0}, {0, 1, 0}, {2, 1, 1}, {1, 0, 0.5}}}},
            {"nonIsoscelesTrapezoid", {{{3, 0.5, 0}, {0, 0.9083, 0.5}, {1, 0.5, 1}, {2, 0.0917, 0.5}}}},
            {"noSmoking", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"notchedRightArrow", {{{0, 0.6333, 0}, {3, 1, 0.5}, {2, 0.6333, 1}, {1, 0.1833, 0.5}}}},
            {"octagon", {{{6, 0.2148, 0}, {1, 1, 0.7071}, {2, 0.7852, 1}, {4, 0, 0.7071}}}},
            {"parallelogram", {{{0, 0.5, 0}, {2, 0.9083, 0.5}, {4, 0.5, 1}, {5, 0.0917, 0.5}}}},
            {"pentagon", {{{0, 0.5, 0}, {5, 1, 0.382}, {3, 0.5, 1}, {1, 0, 0.382}}}},
            {"pie", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"pieWedge", {{{0, 1, 0.5}, {0, 1, 0.5}, {1, 0.5, 1}, {1, 0.5, 1}}}},
            {"plaque", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"plaqueTabs", {{{5, 0.938, 0}, {9, 1, 0.0846}, {7, 0.938, 1}, {1, 0, 0.0846}}}},
            {"plus", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"quadArrow", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"quadArrowCallout", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"rect", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"ribbon", {{{0, 0.5, 0.1667}, {3, 0.875, 0.4167}, {2, 0.5, 1}, {1, 0.125, 0.4167}}}},
            {"ribbon2", {{{0, 0.5, 0}, {3, 0.875, 0.5833}, {2, 0.5, 0.8333}, {1, 0.125, 0.5833}}}},
            {"rightArrow", {{{0, 0.6333, 0}, {3, 1, 0.5}, {2, 0.6333, 1}, {1, 0, 0.5}}}},
            {"rightArrowCallout", {{{0, 0.3249, 0}, {3, 1, 0.5}, {2, 0.3249, 1}, {1, 0, 0.5}}}},
            {"rightBrace", {{{0, 0, 0}, {1, 1, 0.5}, {2, 0, 1}, {0, 0, 0}}}},
            {"rightBracket", {{{0, 0, 0}, {2, 1, 0.5}, {1, 0, 1}, {0, 0, 0}}}},
            {"round1Rect", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"round2DiagRect", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"round2SameRect", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"roundRect", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"rtTriangle", {{{0, 0, 0}, {4, 1, 1}, {3, 0.5, 1}, {1, 0, 0.5}}}},
            {"smileyFace", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"snip1Rect", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"snip2DiagRect", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"snip2SameRect", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"snipRoundRect", {{{3, 0.5, 0}, {0, 1, 0.5}, {1, 0.5, 1}, {2, 0, 0.5}}}},
            {"squareTabs", {{{7, 0.938, 0}, {11, 1, 0.0846}, {9, 0.938, 1}, {1, 0, 0.0846}}}},
            {"star10", {{{8, 0.5, 0}, {1, 1, 0.6545}, {3, 0.5, 1}, {5, 0, 0.6545}}}},
            {"star12", {{{10, 0.5, 0}, {1, 1, 0.5}, {4, 0.5, 1}, {7, 0, 0.5}}}},
            {"star16", {{{14, 0.5, 0}, {2, 1, 0.5}, {6, 0.5, 1}, {10, 0, 0.5}}}},
            {"star24", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"star32", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"star4", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"star5", {{{0, 0.5, 0}, {4, 1, 0.382}, {2, 0.191, 1}, {1, 0, 0.382}}}},
            {"star6", {{{5, 0.5, 0}, {0, 1, 0.25}, {2, 0.5, 1}, {3, 0, 0.75}}}},
            {"star7", {{{6, 0.5, 0}, {1, 1, 0.6431}, {3, 0.2775, 1}, {4, 0, 0.6431}}}},
            {"star8", {{{6, 0.5, 0}, {0, 1, 0.5}, {2, 0.5, 1}, {4, 0, 0.5}}}},
            {"stripedRightArrow", {{{0, 0.6333, 0}, {3, 1, 0.5}, {2, 0.6333, 1}, {1, 0, 0.5}}}},
            {"sun", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"swooshArrow", {{{1, 0.8674, 0}, {3, 0.9088, 0.5}, {0, 0, 1}, {0, 0, 1}}}},
            {"teardrop", {{{6, 0.5, 0}, {0, 1, 0.5}, {2, 0.5, 1}, {4, 0, 0.5}}}},
            {"trapezoid", {{{0, 0.5, 0}, {3, 0.9083, 0.5}, {2, 0.5, 1}, {1, 0.0917, 0.5}}}},
            {"triangle", {{{0, 0.5, 0}, {5, 0.75, 0.5}, {3, 0.5, 1}, {1, 0.25, 0.5}}}},
            {"upArrow", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"upArrowCallout", {{{0, 0.5, 0}, {3, 1, 0.3502}, {2, 0.5, 1}, {1, 0, 0.3502}}}},
            {"upDownArrow", {{{0, 0.5, 0}, {5, 1, 0.5}, {4, 0.5, 1}, {1, 0, 0.5}}}},
            {"upDownArrowCallout", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"uturnArrow", {{{3, 0.4542, 0}, {2, 1, 0.5}, {1, 0.8167, 0.75}, {4, 0.0917, 1}}}},
            {"verticalScroll", {{{0, 0.5, 0}, {3, 0.9083, 0.5}, {2, 0.5, 1}, {1, 0.0917, 0.5}}}},
            {"wave", {{{0, 0.5, 0.125}, {3, 1, 0.5}, {2, 0.5, 0.875}, {1, 0, 0.5}}}},
            {"wedgeEllipseCallout", {{{0, 0.5, 0}, {6, 1, 0.5}, {4, 0.5, 1}, {2, 0, 0.5}}}},
            {"wedgeRectCallout", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
            {"wedgeRoundRectCallout", {{{0, 0.5, 0}, {3, 1, 0.5}, {2, 0.5, 1}, {1, 0, 0.5}}}},
        };
        return table;
    }

    const std::map<std::string, std::vector<std::pair<std::string, long long>>>& shape_adjustments() {
        static const std::map<std::string, std::vector<std::pair<std::string, long long>>> table = {
            {"accentBorderCallout1", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 112500}, {"adj4", -38333}}},
            {"accentBorderCallout2", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 112500}, {"adj6", -46667}}},
            {"accentBorderCallout3", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 100000}, {"adj6", -16667}, {"adj7", 112963}, {"adj8", -8333}}},
            {"accentCallout1", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 112500}, {"adj4", -38333}}},
            {"accentCallout2", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 112500}, {"adj6", -46667}}},
            {"accentCallout3", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 100000}, {"adj6", -16667}, {"adj7", 112963}, {"adj8", -8333}}},
            {"arc", {{"adj1", 16200000}, {"adj2", 0}}},
            {"bentArrow", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 43750}}},
            {"bentConnector3", {{"adj1", 50000}}},
            {"bentConnector4", {{"adj1", 50000}, {"adj2", 50000}}},
            {"bentConnector5", {{"adj1", 50000}, {"adj2", 50000}, {"adj3", 50000}}},
            {"bentUpArrow", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}}},
            {"bevel", {{"adj", 12500}}},
            {"blockArc", {{"adj1", 10800000}, {"adj2", 0}, {"adj3", 25000}}},
            {"borderCallout1", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 112500}, {"adj4", -38333}}},
            {"borderCallout2", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 112500}, {"adj6", -46667}}},
            {"borderCallout3", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 100000}, {"adj6", -16667}, {"adj7", 112963}, {"adj8", -8333}}},
            {"bracePair", {{"adj", 8333}}},
            {"bracketPair", {{"adj", 16667}}},
            {"callout1", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 112500}, {"adj4", -38333}}},
            {"callout2", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 112500}, {"adj6", -46667}}},
            {"callout3", {{"adj1", 18750}, {"adj2", -8333}, {"adj3", 18750}, {"adj4", -16667}, {"adj5", 100000}, {"adj6", -16667}, {"adj7", 112963}, {"adj8", -8333}}},
            {"can", {{"adj", 25000}}},
            {"chevron", {{"adj", 50000}}},
            {"chord", {{"adj1", 2700000}, {"adj2", 16200000}}},
            {"circularArrow", {{"adj1", 12500}, {"adj2", 1142319}, {"adj3", 20457681}, {"adj4", 10800000}, {"adj5", 12500}}},
            {"cloudCallout", {{"adj1", -20833}, {"adj2", 62500}}},
            {"corner", {{"adj1", 50000}, {"adj2", 50000}}},
            {"cube", {{"adj", 25000}}},
            {"curvedConnector3", {{"adj1", 50000}}},
            {"curvedConnector4", {{"adj1", 50000}, {"adj2", 50000}}},
            {"curvedConnector5", {{"adj1", 50000}, {"adj2", 50000}, {"adj3", 50000}}},
            {"curvedDownArrow", {{"adj1", 25000}, {"adj2", 50000}, {"adj3", 25000}}},
            {"curvedLeftArrow", {{"adj1", 25000}, {"adj2", 50000}, {"adj3", 25000}}},
            {"curvedRightArrow", {{"adj1", 25000}, {"adj2", 50000}, {"adj3", 25000}}},
            {"curvedUpArrow", {{"adj1", 25000}, {"adj2", 50000}, {"adj3", 25000}}},
            {"decagon", {{"vf", 105146}}},
            {"diagStripe", {{"adj", 50000}}},
            {"donut", {{"adj", 25000}}},
            {"doubleWave", {{"adj1", 6250}, {"adj2", 0}}},
            {"downArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"downArrowCallout", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 64977}}},
            {"ellipseRibbon", {{"adj1", 25000}, {"adj2", 50000}, {"adj3", 12500}}},
            {"ellipseRibbon2", {{"adj1", 25000}, {"adj2", 50000}, {"adj3", 12500}}},
            {"foldedCorner", {{"adj", 16667}}},
            {"frame", {{"adj1", 12500}}},
            {"gear6", {{"adj1", 15000}, {"adj2", 3526}}},
            {"gear9", {{"adj1", 10000}, {"adj2", 1763}}},
            {"halfFrame", {{"adj1", 33333}, {"adj2", 33333}}},
            {"heptagon", {{"hf", 102572}, {"vf", 105210}}},
            {"hexagon", {{"adj", 25000}, {"vf", 115470}}},
            {"homePlate", {{"adj", 50000}}},
            {"horizontalScroll", {{"adj", 12500}}},
            {"leftArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"leftArrowCallout", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 64977}}},
            {"leftBrace", {{"adj1", 8333}, {"adj2", 50000}}},
            {"leftBracket", {{"adj", 8333}}},
            {"leftCircularArrow", {{"adj1", 12500}, {"adj2", 20457681}, {"adj3", 1142319}, {"adj4", 10800000}, {"adj5", 12500}}},
            {"leftRightArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"leftRightArrowCallout", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 48123}}},
            {"leftRightCircularArrow", {{"adj1", 12500}, {"adj2", 1142319}, {"adj3", 20457681}, {"adj4", 11942319}, {"adj5", 12500}}},
            {"leftRightRibbon", {{"adj1", 50000}, {"adj2", 50000}, {"adj3", 16667}}},
            {"leftRightUpArrow", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}}},
            {"leftUpArrow", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}}},
            {"mathDivide", {{"adj1", 23520}, {"adj2", 5880}, {"adj3", 11760}}},
            {"mathEqual", {{"adj1", 23520}, {"adj2", 11760}}},
            {"mathMinus", {{"adj1", 23520}}},
            {"mathMultiply", {{"adj1", 23520}}},
            {"mathNotEqual", {{"adj1", 23520}, {"adj2", 6600000}, {"adj3", 11760}}},
            {"mathPlus", {{"adj1", 23520}}},
            {"moon", {{"adj", 50000}}},
            {"noSmoking", {{"adj", 18750}}},
            {"nonIsoscelesTrapezoid", {{"adj1", 25000}, {"adj2", 25000}}},
            {"notchedRightArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"octagon", {{"adj", 29289}}},
            {"parallelogram", {{"adj", 25000}}},
            {"pentagon", {{"hf", 105146}, {"vf", 110557}}},
            {"pie", {{"adj1", 0}, {"adj2", 16200000}}},
            {"plaque", {{"adj", 16667}}},
            {"plus", {{"adj", 25000}}},
            {"quadArrow", {{"adj1", 22500}, {"adj2", 22500}, {"adj3", 22500}}},
            {"quadArrowCallout", {{"adj1", 18515}, {"adj2", 18515}, {"adj3", 18515}, {"adj4", 48123}}},
            {"ribbon", {{"adj1", 16667}, {"adj2", 50000}}},
            {"ribbon2", {{"adj1", 16667}, {"adj2", 50000}}},
            {"rightArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"rightArrowCallout", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 64977}}},
            {"rightBrace", {{"adj1", 8333}, {"adj2", 50000}}},
            {"rightBracket", {{"adj", 8333}}},
            {"round1Rect", {{"adj", 16667}}},
            {"round2DiagRect", {{"adj1", 16667}, {"adj2", 0}}},
            {"round2SameRect", {{"adj1", 16667}, {"adj2", 0}}},
            {"roundRect", {{"adj", 16667}}},
            {"smileyFace", {{"adj", 4653}}},
            {"snip1Rect", {{"adj", 16667}}},
            {"snip2DiagRect", {{"adj1", 0}, {"adj2", 16667}}},
            {"snip2SameRect", {{"adj1", 16667}, {"adj2", 0}}},
            {"snipRoundRect", {{"adj1", 16667}, {"adj2", 16667}}},
            {"star10", {{"adj", 42533}, {"hf", 105146}}},
            {"star12", {{"adj", 37500}}},
            {"star16", {{"adj", 37500}}},
            {"star24", {{"adj", 37500}}},
            {"star32", {{"adj", 37500}}},
            {"star4", {{"adj", 12500}}},
            {"star5", {{"adj", 19098}, {"hf", 105146}, {"vf", 110557}}},
            {"star6", {{"adj", 28868}, {"hf", 115470}}},
            {"star7", {{"adj", 34601}, {"hf", 102572}, {"vf", 105210}}},
            {"star8", {{"adj", 37500}}},
            {"stripedRightArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"sun", {{"adj", 25000}}},
            {"swooshArrow", {{"adj1", 25000}, {"adj2", 16667}}},
            {"teardrop", {{"adj", 100000}}},
            {"trapezoid", {{"adj", 25000}}},
            {"triangle", {{"adj", 50000}}},
            {"upArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"upArrowCallout", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 64977}}},
            {"upDownArrow", {{"adj1", 50000}, {"adj2", 50000}}},
            {"upDownArrowCallout", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 48123}}},
            {"uturnArrow", {{"adj1", 25000}, {"adj2", 25000}, {"adj3", 25000}, {"adj4", 43750}, {"adj5", 75000}}},
            {"verticalScroll", {{"adj", 12500}}},
            {"wave", {{"adj1", 12500}, {"adj2", 0}}},
            {"wedgeEllipseCallout", {{"adj1", -20833}, {"adj2", 62500}}},
            {"wedgeRectCallout", {{"adj1", -20833}, {"adj2", 62500}}},
            {"wedgeRoundRectCallout", {{"adj1", -20833}, {"adj2", 62500}, {"adj3", 16667}}},
        };
        return table;
    }

    namespace {
        // SVG path의 명령과 숫자를 차례로 읽는다
        class PathReader {
        public:
            explicit PathReader(const std::string& text) : text_(text) {}

            bool at_end() {
                skip_separators();
                return pos_ >= text_.size();
            }

            // 다음이 명령 글자면 읽어서 돌려준다
            std::optional<char> command() {
                skip_separators();
                if (pos_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[pos_]))) {
                    return text_[pos_++];
                }
                return std::nullopt;
            }

            std::optional<double> number() {
                skip_separators();
                if (pos_ >= text_.size()) {
                    return std::nullopt;
                }
                const char c = text_[pos_];
                if (c != '-' && c != '+' && c != '.' && !std::isdigit(static_cast<unsigned char>(c))) {
                    return std::nullopt;
                }
                char* end = nullptr;
                const double value = std::strtod(text_.c_str() + pos_, &end);
                if (end == text_.c_str() + pos_) {
                    return std::nullopt;
                }
                pos_ = static_cast<std::size_t>(end - text_.c_str());
                return value;
            }

            // 호의 플래그는 0 또는 1 한 글자이고 뒤에 구분자가 없어도 된다
            std::optional<bool> flag() {
                skip_separators();
                if (pos_ < text_.size() && (text_[pos_] == '0' || text_[pos_] == '1')) {
                    return text_[pos_++] == '1';
                }
                return std::nullopt;
            }

        private:
            const std::string& text_;
            std::size_t pos_ = 0;

            void skip_separators() {
                while (pos_ < text_.size() && (std::isspace(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == ',')) {
                    ++pos_;
                }
            }
        };

        double angle_between(Point u, Point v) {
            return std::atan2(u.x * v.y - u.y * v.x, u.x * v.x + u.y * v.y);
        }

        // SVG 명세(F.6.5)대로 호를 중심과 각도로 바꾼 뒤 90도 이하 조각마다 3차 베지어로 근사한다
        void append_arc(std::vector<PathSegment>& segments, Point from, double rx, double ry, double rotation, bool large, bool sweep, Point to) {
            if (from.x == to.x && from.y == to.y) {
                return;
            }
            rx = std::abs(rx);
            ry = std::abs(ry);
            if (rx == 0 || ry == 0) {
                segments.push_back({'L', {to}});
                return;
            }
            const double phi = rotation * std::numbers::pi / 180;
            const double cos_phi = std::cos(phi);
            const double sin_phi = std::sin(phi);
            const double dx = (from.x - to.x) / 2;
            const double dy = (from.y - to.y) / 2;
            const double x1 = cos_phi * dx + sin_phi * dy;
            const double y1 = -sin_phi * dx + cos_phi * dy;
            const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
            if (lambda > 1) {
                rx *= std::sqrt(lambda);
                ry *= std::sqrt(lambda);
            }
            const double numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
            const double denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
            const double coefficient = (large == sweep ? -1 : 1) * std::sqrt(std::max(0.0, numerator / denominator));
            const double cx1 = coefficient * rx * y1 / ry;
            const double cy1 = -coefficient * ry * x1 / rx;
            const double cx = cos_phi * cx1 - sin_phi * cy1 + (from.x + to.x) / 2;
            const double cy = sin_phi * cx1 + cos_phi * cy1 + (from.y + to.y) / 2;
            const Point u{(x1 - cx1) / rx, (y1 - cy1) / ry};
            const Point v{(-x1 - cx1) / rx, (-y1 - cy1) / ry};
            const double start = angle_between({1, 0}, u);
            double sweep_angle = angle_between(u, v);
            if (!sweep && sweep_angle > 0) {
                sweep_angle -= 2 * std::numbers::pi;
            } else if (sweep && sweep_angle < 0) {
                sweep_angle += 2 * std::numbers::pi;
            }
            const int count = std::max(1, static_cast<int>(std::ceil(std::abs(sweep_angle) / (std::numbers::pi / 2) - 1e-9)));
            const double step = sweep_angle / count;
            const double k = 4.0 / 3.0 * std::tan(step / 4);
            const auto point = [&](double t) {
                return Point{cx + rx * cos_phi * std::cos(t) - ry * sin_phi * std::sin(t), cy + rx * sin_phi * std::cos(t) + ry * cos_phi * std::sin(t)};
            };
            const auto derivative = [&](double t) {
                return Point{-rx * cos_phi * std::sin(t) - ry * sin_phi * std::cos(t), -rx * sin_phi * std::sin(t) + ry * cos_phi * std::cos(t)};
            };
            for (int i = 0; i < count; ++i) {
                const double t1 = start + i * step;
                const double t2 = t1 + step;
                const Point p1 = point(t1);
                const Point p2 = point(t2);
                const Point d1 = derivative(t1);
                const Point d2 = derivative(t2);
                segments.push_back({'C', {{p1.x + k * d1.x, p1.y + k * d1.y}, {p2.x - k * d2.x, p2.y - k * d2.y}, i + 1 == count ? to : p2}});
            }
        }
    }

    std::optional<ParsedPath> parse_svg_path(const std::string& text, std::string& error) {
        PathReader reader(text);
        ParsedPath result;
        Point current;
        Point start;
        Point control;      // 앞 C, S의 두 번째 제어점 또는 앞 Q, T의 제어점
        char previous = 0;  // 앞 명령 (대문자)
        char command = 0;
        const auto fail = [&](const std::string& message) -> std::optional<ParsedPath> {
            error = message;
            return std::nullopt;
        };
        while (!reader.at_end()) {
            if (const auto next = reader.command()) {
                command = *next;
            } else if (command == 0) {
                return fail("a path must start with M");
            } else if (command == 'Z' || command == 'z') {
                return fail("expected a command after Z");
            }
            const bool relative = std::islower(static_cast<unsigned char>(command));
            const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
            if (result.segments.empty() && upper != 'M') {
                return fail("a path must start with M");
            }
            const auto read_point = [&](Point& point) {
                const auto x = reader.number();
                const auto y = x ? reader.number() : std::nullopt;
                if (!x || !y) {
                    return false;
                }
                point = relative ? Point{current.x + *x, current.y + *y} : Point{*x, *y};
                return true;
            };
            Point p;
            Point c1;
            Point c2;
            switch (upper) {
                case 'M':
                    if (!read_point(p)) {
                        return fail("expected x y after M");
                    }
                    result.segments.push_back({'M', {p}});
                    current = start = p;
                    // M 뒤에 이어지는 좌표는 L이다
                    command = relative ? 'l' : 'L';
                    break;
                case 'L':
                    if (!read_point(p)) {
                        return fail("expected x y after L");
                    }
                    result.segments.push_back({'L', {p}});
                    current = p;
                    break;
                case 'H':
                case 'V': {
                    const auto value = reader.number();
                    if (!value) {
                        return fail(std::string("expected a number after ") + upper);
                    }
                    p = current;
                    (upper == 'H' ? p.x : p.y) = relative ? (upper == 'H' ? current.x : current.y) + *value : *value;
                    result.segments.push_back({'L', {p}});
                    current = p;
                    break;
                }
                case 'C':
                    if (!read_point(c1) || !read_point(c2) || !read_point(p)) {
                        return fail("expected 3 points after C");
                    }
                    result.segments.push_back({'C', {c1, c2, p}});
                    control = c2;
                    current = p;
                    break;
                case 'S':
                    c1 = previous == 'C' || previous == 'S' ? Point{2 * current.x - control.x, 2 * current.y - control.y} : current;
                    if (!read_point(c2) || !read_point(p)) {
                        return fail("expected 2 points after S");
                    }
                    result.segments.push_back({'C', {c1, c2, p}});
                    control = c2;
                    current = p;
                    break;
                case 'Q':
                case 'T': {
                    Point q = previous == 'Q' || previous == 'T' ? Point{2 * current.x - control.x, 2 * current.y - control.y} : current;
                    if ((upper == 'Q' && !read_point(q)) || !read_point(p)) {
                        return fail(std::string("expected ") + (upper == 'Q' ? "2 points" : "a point") + " after " + upper);
                    }
                    // 2차 베지어는 같은 모양의 3차 베지어로 바꾼다
                    result.segments.push_back({'C', {{current.x + 2.0 / 3 * (q.x - current.x), current.y + 2.0 / 3 * (q.y - current.y)},
                                                     {p.x + 2.0 / 3 * (q.x - p.x), p.y + 2.0 / 3 * (q.y - p.y)}, p}});
                    control = q;
                    current = p;
                    break;
                }
                case 'A': {
                    const auto rx = reader.number();
                    const auto ry = rx ? reader.number() : std::nullopt;
                    const auto rotation = ry ? reader.number() : std::nullopt;
                    const auto large = rotation ? reader.flag() : std::nullopt;
                    const auto sweep = large ? reader.flag() : std::nullopt;
                    if (!sweep || !read_point(p)) {
                        return fail("expected rx ry rotation large-arc sweep x y after A");
                    }
                    append_arc(result.segments, current, *rx, *ry, *rotation, *large, *sweep, p);
                    current = p;
                    break;
                }
                case 'Z':
                    result.segments.push_back({'Z', {}});
                    current = start;
                    break;
                default:
                    return fail(std::string("unknown path command '") + command + "'");
            }
            previous = upper;
        }
        if (result.segments.empty()) {
            return fail("the path is empty");
        }
        bool first = true;
        for (const auto& segment : result.segments) {
            for (const auto& point : segment.points) {
                if (first) {
                    result.min = result.max = point;
                    first = false;
                }
                result.min = {std::min(result.min.x, point.x), std::min(result.min.y, point.y)};
                result.max = {std::max(result.max.x, point.x), std::max(result.max.y, point.y)};
            }
        }
        return result;
    }

    namespace {
        // DrawingML의 xfrm: 상자 안의 좌표를 뒤집고 가운데를 중심으로 돌린다
        struct Frame {
            double x;
            double y;
            double width;
            double height;
            int rotation;
            bool flip_h;
            bool flip_v;

            Point point(Point local) const {
                const double u = (flip_h ? width - local.x : local.x) - width / 2;
                const double v = (flip_v ? height - local.y : local.y) - height / 2;
                const double angle = rotation * std::numbers::pi / 180;
                return {x + width / 2 + u * std::cos(angle) - v * std::sin(angle), y + height / 2 + u * std::sin(angle) + v * std::cos(angle)};
            }

            // 바깥 방향을 상자 안의 방향으로
            Point local_direction(Point d) const {
                const double angle = -rotation * std::numbers::pi / 180;
                Point r{std::round(d.x * std::cos(angle) - d.y * std::sin(angle)), std::round(d.x * std::sin(angle) + d.y * std::cos(angle))};
                return {flip_h ? -r.x : r.x, flip_v ? -r.y : r.y};
            }
        };

        long long ratio(double value, double length) {
            return std::llround(value / std::max(length, 1.0) * 100000);
        }
    }

    ConnectorGeometry route_connector(const std::string& kind, Point start, Point start_out, Point end, Point end_out, double margin) {
        const double dx = end.x - start.x;
        const double dy = end.y - start.y;
        if (kind == "straight") {
            return {"straightConnector1", 0, dx < 0, dy < 0, std::min(start.x, end.x), std::min(start.y, end.y), std::abs(dx), std::abs(dy), {}};
        }
        // 0, 90, 180, 270도와 뒤집기를 모두 해 보고, 선이 start에서 시작해 상자 안의 x 방향으로 나가는 것 중
        // 꺾이는 수가 가장 적은 모양을 고른다
        const std::string family = kind == "curved" ? "curvedConnector" : "bentConnector";
        std::optional<ConnectorGeometry> best;
        int best_score = 0;
        for (const int rotation : {0, 90, 180, 270}) {
            const bool turned = rotation % 180 != 0;
            const double width = turned ? std::abs(dy) : std::abs(dx);
            const double height = turned ? std::abs(dx) : std::abs(dy);
            for (const bool flip_h : {false, true}) {
                for (const bool flip_v : {false, true}) {
                    const Frame frame{(start.x + end.x - width) / 2, (start.y + end.y - height) / 2, width, height, rotation, flip_h, flip_v};
                    const Point origin = frame.point({0, 0});
                    if (std::abs(origin.x - start.x) > 1 || std::abs(origin.y - start.y) > 1) {
                        continue;
                    }
                    const Point out = frame.local_direction(start_out);
                    if (out.y != 0) {
                        continue;
                    }
                    const double sx = out.x;
                    // 끝점에 들어가는 방향은 끝 변의 바깥쪽과 반대다
                    const Point arrive = frame.local_direction({-end_out.x, -end_out.y});
                    ConnectorGeometry geometry{"", rotation, flip_h, flip_v, frame.x, frame.y, width, height, {}};
                    int score = 0;
                    if (arrive.y == 0) {
                        const double ax = arrive.x;
                        if (sx > 0 && ax > 0) {
                            geometry.preset = family + "3";
                            geometry.adjust = {50000};
                            score = 30;
                        } else if (sx > 0) {
                            geometry.preset = family + "3";
                            geometry.adjust = {ratio(width + margin, width)};
                            score = 31;
                        } else if (ax > 0) {
                            geometry.preset = family + "3";
                            geometry.adjust = {ratio(-margin, width)};
                            score = 31;
                        } else {
                            geometry.preset = family + "5";
                            geometry.adjust = {ratio(-margin, width), 50000, ratio(width + margin, width)};
                            score = 50;
                        }
                    } else {
                        const double ay = arrive.y;
                        if (sx > 0 && ay > 0) {
                            geometry.preset = family + "2";
                            score = 20;
                        } else {
                            geometry.preset = family + "4";
                            const double x1 = sx > 0 ? width / 2 : -margin;
                            const double y2 = ay > 0 ? height / 2 : height + margin;
                            geometry.adjust = {ratio(x1, width), ratio(y2, height)};
                            score = 40;
                        }
                    }
                    if (!best || score < best_score) {
                        best = geometry;
                        best_score = score;
                    }
                }
            }
        }
        if (!best) {
            return {"straightConnector1", 0, dx < 0, dy < 0, std::min(start.x, end.x), std::min(start.y, end.y), std::abs(dx), std::abs(dy), {}};
        }
        return *best;
    }

    namespace {
        // side(top, right, bottom, left) 쪽 연결점의 자리와 바깥 방향
        std::optional<AnchorPoint> anchor_point(const Anchor& anchor, const std::string& side) {
            const auto sites = connection_sites().find(anchor.kind);
            if (sites == connection_sites().end()) {
                return std::nullopt;
            }
            static const std::array<std::string, 4> sides = {"top", "right", "bottom", "left"};
            const auto index = static_cast<std::size_t>(std::find(sides.begin(), sides.end(), side) - sides.begin());
            // 뒤집힌 도형은 보이는 쪽과 도형 안의 쪽이 반대다
            std::size_t geometry = index;
            if (anchor.flip_h && index % 2 == 1) {
                geometry = 4 - index;
            }
            if (anchor.flip_v && index % 2 == 0) {
                geometry = 2 - index;
            }
            const Site& site = sites->second.at(geometry);
            double u = site.x * anchor.width - anchor.width / 2;
            double v = site.y * anchor.height - anchor.height / 2;
            if (anchor.flip_h) {
                u = -u;
            }
            if (anchor.flip_v) {
                v = -v;
            }
            const double c = std::cos(anchor.rotation);
            const double s = std::sin(anchor.rotation);
            const Point center{anchor.x + anchor.width / 2, anchor.y + anchor.height / 2};
            static const std::array<Point, 4> normals = {Point{0, -1}, Point{1, 0}, Point{0, 1}, Point{-1, 0}};
            const Point normal{normals[index].x * c - normals[index].y * s, normals[index].x * s + normals[index].y * c};
            const Point out = std::abs(normal.x) >= std::abs(normal.y) ? Point{normal.x > 0 ? 1.0 : -1.0, 0} : Point{0, normal.y > 0 ? 1.0 : -1.0};
            return AnchorPoint{site.index, {center.x + u * c - v * s, center.y + u * s + v * c}, out};
        }
    }

    std::optional<ConnectorPlan> plan_connector(const Anchor& from, const Anchor& to, std::string from_side, std::string to_side, const std::string& kind, double margin) {
        const double dx = (to.x + to.width / 2) - (from.x + from.width / 2);
        const double dy = (to.y + to.height / 2) - (from.y + from.height / 2);
        const bool horizontal = std::abs(dx) >= std::abs(dy);
        if (from_side.empty() || from_side == "auto") {
            from_side = horizontal ? (dx >= 0 ? "right" : "left") : (dy >= 0 ? "bottom" : "top");
        }
        if (to_side.empty() || to_side == "auto") {
            to_side = horizontal ? (dx >= 0 ? "left" : "right") : (dy >= 0 ? "top" : "bottom");
        }
        const auto start = anchor_point(from, from_side);
        const auto end = anchor_point(to, to_side);
        if (!start || !end) {
            return std::nullopt;
        }
        return ConnectorPlan{route_connector(kind.empty() ? "straight" : kind, start->point, start->out, end->point, end->out, margin), *start, *end};
    }

    ImageFit fit_image(ImageFit frame, const std::string& fit, std::optional<std::pair<int, int>> image_size) {
        if ((fit != "cover" && fit != "contain") || !image_size || frame.width <= 0 || frame.height <= 0) {
            return frame;
        }
        auto& crop = frame.crop;
        const double visible_width = 1 - crop[0] - crop[2];
        const double visible_height = 1 - crop[1] - crop[3];
        const double image_ratio = image_size->first * visible_width / (image_size->second * visible_height);
        const double frame_ratio = frame.width / frame.height;
        if (fit == "cover" && image_ratio > frame_ratio) {
            const double extra = (1 - frame_ratio / image_ratio) * visible_width / 2;
            crop[0] += extra;
            crop[2] += extra;
        } else if (fit == "cover") {
            const double extra = (1 - image_ratio / frame_ratio) * visible_height / 2;
            crop[1] += extra;
            crop[3] += extra;
        } else if (image_ratio > frame_ratio) {
            const double height = frame.width / image_ratio;
            frame.y += (frame.height - height) / 2;
            frame.height = height;
        } else {
            const double width = frame.height * image_ratio;
            frame.x += (frame.width - width) / 2;
            frame.width = width;
        }
        return frame;
    }
}
