/*
 * Stoatworks Labs - About window data for Gauge.
 *
 * PROVISIONAL: a hand copy adapted from rebate's, in the shape
 * stoatworks-backend/scripts/sync-about.py generates, because gauge is not in the
 * website's projects.json yet. The first sync after it is registered overwrites
 * this file. Until then there is no guide and no project page, so those two are
 * empty and their buttons are left out.
 *
 * `version` here is a fallback read from this repo's own manifest at sync
 * time. Anything with a build step injects the real one at build time and
 * overrides this.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Gauge";
    inline constexpr auto slug = "gauge";
    inline constexpr auto hook = "The film strip itself, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/gauge";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
