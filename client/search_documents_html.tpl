{% extends "search_resultlist_html.tpl" %}

{% block relatedblock %}
{% if relatedterms %}
<div id="relatedresult">
<ul>
{% for result in relatedterms %}
<li onclick="parent.location='evalQuery.php?q={{ result.encvalue }}&s={{ scheme }}'">
<div id="related">
<div id="related_term">{{ result.value }}</div>
<div id="related_weight">{{ "%.4f" % result.weight }}</div>
</div>
</li>
{% end %}
</ul>
</div>
{% end %}
{% end %}

{% block nblinksblock %}
{% if nblinks %}
<div id="nblinkresult">
<ul>
{% for nblink in nblinks %}
{% set enclink = nblink.title.replace(' ','_') %}
<li onclick="parent.location='https://en.wikipedia.org/wiki/{{ enclink }}'">
<div id="nblink">
<div id="nblink_term">{{ nblink.title }}</div>
<div id="nblink_weight">{{ "%.4f" % nblink.weight }}</div>
</div>
</li>
{% end %}
</ul>
</div>
{% end %}
{% end %}

{% block featuresblock %}
{% if features %}
<div id="featureresult">
<ul>
{% for feature in features %}
<li>
<div id="featurelink">
<div id="featurelink_term">{{ feature.title }}</div>
<div id="featurelink_weight">{{ "%.4f" % feature.weight }}</div>
</div>
</li>
{% end %}
</ul>
</div>
{% end %}
{% end %}

{% block resultblock %}
<div id="searchresult">
<ul>
{% for resultidx,result in enumerate(results) %}
{% set enclink = result.title.replace(' ','_') %}
<li onclick="parent.location='https://en.wikipedia.org/wiki/{{ enclink }}'">
<h3>{{ result.title }}</h3>
<div id="rank">
{% if mode == "debug" %}
<div id="rank_docno">{{ result.docno }}</div>
<div id="rank_weight">{{ "%.4f" % result.weight }}</div>
{% end %}
{% if result.paratitle %}
<div id="rank_paratitle"> {% raw result.paratitle %} </div>
{% end %}
<div id="rank_abstract"> {% raw result.abstract %} </div>
</div>
</li>

{% if result.debuginfo %}
<div id="rank_debuginfo">
<input class="toggle-box" id="DebugInfo{{resultidx}}_usage" type="checkbox" >
<label for="DebugInfo{{resultidx}}_usage">Debug</label>
<pre>
{% raw result.debuginfo %}
</pre>
</div>
{% end %}
{% end %}

</ul>
</div>
{% end %}

