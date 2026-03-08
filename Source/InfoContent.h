#pragma once

// ============================================================================
// InfoContent.h — Structured content for the FREQ-TR info popup.
// ============================================================================

namespace InfoContent
{
    static constexpr const char* version = "1.0c";

    static constexpr const char* xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<info>
  <content>
    <heading>FREQ-TR v1.0c</heading>
    <spacer/>
    <text>by Nemester</text>
    <link url="https://github.com/lmaser/FREQ-TR">Github Repository</link>
    <separator>&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;</separator>
    <spacer/>
    <poem>"If you want to find</poem>
    <poem>the secrets of the universe,</poem>
    <poem>think in terms of energy,</poem>
    <poem>frequency and vibration."</poem>
    <spacer/>
    <text>&#x2014; Nikola Tesla</text>
  </content>
</info>
)xml";
}
