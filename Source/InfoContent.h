#pragma once

// ============================================================================
// InfoContent.h — Structured content for the FREQ-TR info popup.
// ============================================================================

namespace InfoContent
{
    static constexpr const char* version = "1.2";

    static constexpr const char* xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<info>
  <content>
    <heading>FREQ-TR v1.2</heading>
    <spacer/>
    <text>by Nemester</text>
    <link url="https://github.com/lmaser/FREQ-TR">Github Repository</link>
    <separator>&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;</separator>
    <link url="https://ko-fi.com/nemester">Support on Ko-fi</link>
  </content>
</info>
)xml";
}
