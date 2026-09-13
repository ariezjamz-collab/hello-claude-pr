=== Church Magazine ===
Contributors: churchmagazine
Tags: church, magazine, digital publication, table of contents
Requires at least: 5.8
Tested up to: 6.6
Requires PHP: 7.4
Stable tag: 1.0.0
License: GPLv2 or later
License URI: https://www.gnu.org/licenses/gpl-2.0.html

Publish digital magazine issues for your church website with a table of contents on the left and the reading content in the center.

== Description ==

Church Magazine adds two content types to your site:

* **Magazine Issues** — a single publication, e.g. "Fall 2026". Set a cover image, a short description, and optionally attach a downloadable PDF.
* **Magazine Articles** — the individual sections/stories inside an issue (e.g. "Pastor's Note", "Youth Ministry Update"). Each article is assigned to an issue and given an order number.

On the front end, visitors open an issue and see:

* A **left-hand menu** listing every article in the issue as a table of contents.
* The **magazine content in the center**, with each article rendered as its own section.
* Clicking a menu item **jumps straight to that article** with a smooth scroll, and the menu highlights whichever article is currently in view.
* On mobile, the menu collapses behind a "Contents" button to keep the reading area full-width.

Everything is managed from one **Church Magazine** menu in the WordPress admin, with an overview dashboard, quick "New Issue" / "New Article" buttons, and a Settings page to change sidebar colors and width without touching code.

= Shortcodes =

* `[church_magazine]` — embeds the latest issue's full reading experience into any page or post.
* `[church_magazine id="123"]` — embeds a specific issue by its post ID.
* `[church_magazine_latest]` — a small "Latest Issue" teaser card with cover image and a "Read Now" button, handy for a homepage.

== Installation ==

1. Upload the `church-magazine` folder to `/wp-content/plugins/`.
2. Activate the plugin through the "Plugins" screen in WordPress.
3. Go to the new **Church Magazine** menu to create your first Issue and its Articles.
4. Visit the Issue on the front end to see the table of contents and reading view.

== Frequently Asked Questions ==

= How do I control the order of articles? =

Open an article and use the "Order" field in the Page Attributes box. Lower numbers appear first in the table of contents.

= Can I change the sidebar colors? =

Yes — go to Church Magazine → Settings to set the sidebar background, text color, accent color, and width.

= Can my theme override the layout? =

Yes. Add `single-magazine_issue.php` or `archive-magazine_issue.php` to your theme and it will be used instead of the plugin's built-in template.

== Changelog ==

= 1.0.0 =
* Initial release.
